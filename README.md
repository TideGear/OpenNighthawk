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
options. Thirty-three catalogued fixes and balance options are enabled by
default for normal play, including D1REAL and D1PROX. `--fix none` restores
the original behavior. The headless reference runner and ordinary parity
gates keep fixes off; recorded sessions restore their recorded fix set
(older logs mean no fixes). The separate `--fix QUIZ` option restores the
original aircraft-identification quiz. It is off by default and is enabled
only when named explicitly; `--fix all` does not enable it.

**On top of that parity, OpenNighthawk adds** (none of it changes the game's
logic unless it says it is a fix; "optional" means off until you ask for it):

- **On by default**
  - *Real-time pace*: fifteen simulation steps a second whatever the PC's speed
    (the D1REAL fix), so the world no longer runs faster on a fast machine.
  - *Thirty-three switchable fixes* for the original's bugs (missiles, speech
    hangs, supply-drop credit, secret airstrips, the table overrun). `--fixes
    original` plays the shipped game, bugs included. Saves remember their fix
    set and warn once if you switch.
  - *Works from your GOG or Steam install* without ever writing to it; saves
    live in `%APPDATA%\F117Recomp`.
  - *Every flight is recorded* (input log plus the starting save) and can be
    watched again with `--watch`, with seek, speed and jump keys.
  - *An in-game settings menu* (the ` key) with a description for every
    option, changed values in blue, and a Keys tab.
  - *Key remapping* (all 101 game keys) and *controller support* (dead zone,
    inversion, fire buttons, any button sent as a key).
  - *Sound*: AdLib through an OPL emulator, the PC speaker with smoothed
    digitised speech, and Roland music through Munt using your own MT-32 ROMs
    (or Windows MIDI).
  - *Screenshots* with Tab, a settings file (`f117a.ini`), and a windowed or
    fullscreen picture at 4:3 or with square pixels.
- **Optional visuals** (experimental; the original picture stays the verified
  one)
  - *Enhanced*: the same picture redrawn up to 4K, with in-between frames at
    your display's refresh rate.
  - *Remastered*: the 3-D view redrawn by this project's own renderer, with a
    widescreen view, depth-correct drawing, scenery drawn far past the
    original's reach (Draw distance, Terrain dots, Far object order) and
    smooth motion between the game's steps. Road and coastline jitter is
    gone, and `--remaster-view 4:3` keeps the original's framing with it.
  - *GPU compositor and GPU present path*, on by default for Remastered in a
    real window on Windows with a Direct3D 12 GPU (about 50 to 60 frames a
    second at 4K in the author's flights; the CPU compositor is used without
    a GPU). `F117_GPU_COMPOSE=0` and `F117_GPU_PRESENT=0` turn them off. Its
    pictures equal the CPU compositor's exactly (checked pixel by pixel).
  - *Model detail* (Full by default in Remastered): every building and object
    in its most detailed version at any distance, so lights no longer appear
    suddenly as you fly closer.
  - *CRT look* (scanlines, phosphor mask, glow).
- **Optional sound**
  - *Hybrid sound*: Roland music with AdLib effects together, or the Macintosh
    version's recordings in place of the DOS sounds, from files you supply.
  - *A fixed Munt seed* so the same input renders the same audio.
- **Optional accessibility and play options**
  - *Subtitles* for the radio calls and a *colour-vision aid* for the HUD.
  - *`--fix QUIZ`* brings back the aircraft-identification quiz on the GOG and Steam releases (both ship the same START.EXE, with the quiz switched off).
  - *A 386DX/33 timing profile* to play at a 1991 PC's pace.

## Quick start

1. **Own the game.** Buy F-117A on [Steam](https://store.steampowered.com/app/328920/) or
   [GOG](https://www.gog.com/en/game/f117a_nighthawk_stealth_fighter_20) and install it
   ([why](#you-need-your-own-copy-of-the-game)).
2. **Build.** On 64-bit Windows, download the setup ZIP from [Releases](https://github.com/TideGear/OpenNighthawk/releases/latest), extract it and double-click `setup.cmd`. It finds and verifies your GOG or Steam copy, asks a few questions (install folder, shortcuts, optional sound and picture settings), downloads a small Zig compiler and its other build tools, builds on your PC in about 5 to 15 minutes and installs the game. It downloads its own tools and needs none installed. See [Building](#building).
3. **Play.** Open the OpenNighthawk desktop shortcut or Start menu entry. Answer SETUP's questions as in 1991
   (2 for AdLib). Press the ` key for the settings menu. All options: [Running](#running).

## Status

Progress: **P1 100.00%, P2 96.93%, P3 94.03%, All 97.55%**. Work commit titles start with the
current figures (`py tools/progress.py --title`); [docs/progress.md](docs/progress.md) says what
they measure and [docs/roadmap.md](docs/roadmap.md) what is done and what is left.

| Area | Where it stands |
|---|---|
| **The game loop** | All of it runs: SETUP, the MicroProse logo, the intro, the front end (roster, pilot form, briefing, arming, hangar), takeoff, flight, the debriefing (END) and back. All three sound drivers work: AdLib (music and digitised speech through an OPL emulator), the PC speaker, and Roland (Munt synthesis with your MT-32 ROMs, or Windows MIDI; exact rendered Roland parity is still open). |
| **Translated** | All 17 code files and the LZEXE decompressor: 89,443 instruction starts, 96% of the bytes of the code areas (the rest is strings, tables and variables). On the scripted sessions no game instruction is interpreted under the default timing profile; only the emulated BIOS's own stubs are. |
| **Named and matched** | 1,646 routines have named, explained C equivalents proved against the original's instructions. They run 160,015 of the 163,907 instruction bytes the scripted routes execute; with 2,631 bytes of reviewed code that stays original by design, that is the **gameplay score, 99.23%**. Across all 17 code files, named code covers 191,937 of 209,298 creditable bytes (91.71%). Reviewed references account for 47,734 of 47,952 previous-scope bytes outside routes. |
| **Parity, measured** | 63 scripted routes, run as 62 sessions, end in identical state under the reference interpreter and the recompiled code (all of memory, the registers and every byte sent to the sound card, the palette, MIDI and the disk), checked every 50 million clocks. They cover boot to flight, full sorties through the debriefing, all three sound drivers, all nine theatres, every primary objective type, the pilot roster and the career to retirement ([every route](tools/routes/README.md)). Every translated instruction, reached or not, is also compared from random machine states (0 differences), the translator is checked against 8088 and 80286 silicon test vectors, and planted defects in the generated code are caught. |
| **Other machines** | The emulated PC is held to GOG's DOSBox (the machine the game is sold on: the machine probe, the intro's pictures and music), DOSBox-X, and 86Box running a 386DX/33 with MS-DOS 5.00. The 386DX/33 timing profile plays at a period PC's pace. |
| **Releases** | Steam's game files are byte-identical to GOG's (`tools/verify_install.py`), and the app finds its install. |
| **Visuals** | Optional sharper and smoother visuals (finer polygon geometry and HUD strokes up to 4K, original bitmap text and artwork kept) are **experimental**; the original picture is the verified one. See [Presentation](#presentation). |
| **What is left** | The remaining 1,261 route-executed unmatched bytes (most in the sound drivers) and the code no route runs; reviewed entries for the 218 bytes outside the routes that have none (`py tools/progress.py`); a closing review of indirect entries ([Phase 2](docs/roadmap.md#phase-2---understood-code)). Not scored: play by a person at the keyboard, rendered Roland output against an independent reference and listening checks, refresh rates above 60 Hz and other hardware ([Phase 4](docs/roadmap.md#phase-4---checked-by-people-and-independent-references)). |
| **Re-release features** | Partly built: see the list above and the [roadmap](docs/roadmap.md#phase-3---fixes-and-enhancements-switchable). The game's logic is the same under every presentation. |

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
byte-identical to the one in `f11704 (473.04 Update).zip`). Steam's game files are byte-identical to GOG's.
`py tools/verify_install.py --data "C:\GOG Games\F-117A"` checks your copy
against the SHA-256 of every file the project reads.

**Test evidence follows the same rule.** Routes commit only the inputs a
player would type or click; every capture, screenshot, recording, save and
memory dump they produce is written to a private work directory, never into
the repository. If you find anything in this repository that breaches this,
please open an issue: that is a bug, and a serious one.

## Building

On 64-bit Windows, download the setup ZIP from the
[Releases](https://github.com/TideGear/OpenNighthawk/releases/latest) page
(`OpenNighthawk-setup-*.zip`, source code only), extract it and double-click
`setup.cmd`. Setup downloads its own Python, portable CMake,
Ninja, shader compiler and Zig 0.14.1, checking pinned SHA-256 values. It
finds GOG and Steam installations and verifies the game files before building.
Setup downloads its own Python and build tools and does not use Git. Internet
access is needed for setup. The standard-account test used no developer tools
on PATH, but Visual Studio and Python remained installed elsewhere on that PC;
a PC with neither installed has not yet been verified.

The game is translated and compiled on your machine from your own copy.
Source releases contain no game data, generated C or prebuilt game executable.
Setup installs the game (about 35 MB) to `%LOCALAPPDATA%\Programs\OpenNighthawk`
unless you choose another folder, adds the shortcuts you ask for and an entry in
Windows Settings, Apps, and removes its build files (about 1.4 GB) when it
finishes. Your pilots are kept outside the install folder, and running setup
again updates an existing install in place. Original game files are read, not
modified. Optional MT-32 ROMs and Macintosh sounds remain yours to supply; none
are downloaded. Windows SmartScreen or an antivirus may warn about the new
program the first time; see the [setup guide](docs/setup-user-guide.md).

To uninstall, use Windows Settings, Apps, OpenNighthawk, or double-click
`uninstall.cmd` in the install folder (it asks before deleting your pilots).

If automatic discovery fails, run `setup.cmd -Data "C:\path\to\F-117A"`
from the extracted source folder. `-InstallDir "D:\OpenNighthawk"` selects the
install folder, and `-AcceptLicences` takes every default without questions
(more options: the [setup guide](docs/setup-user-guide.md)). Errors stop setup
and explain what is missing or failed.

**Visual Studio fallback.** If Zig cannot build on your machine, install
Visual Studio Build Tools with Desktop development with C++ and a Windows
SDK, then run `setup.cmd -Compiler VisualStudio`. This fallback can require
administrator access and several GB. Setup uses an installed toolset; it
does not silently install Visual Studio. Rerunning setup cleans its recognized
incomplete work folder and restarts.

On the development PC the Zig build takes about five minutes (4 minutes 30
seconds on 7 November 2026). The Zig-built game gives the same pictures and
the same machine state as the Visual Studio build on the checks run so far
(901 of 901 picture checksums at three draw distances, identical end-of-run
state). Setup was tested end to end under Windows PowerShell 5.1 in a fresh
standard account, and by the author's interactive runs of the wizard. It has
not yet been run on a second PC or one with no Visual Studio or Python
installed anywhere. Validation and limits:
[setup validation](docs/zig-setup-validation.md).

Developers can still use `build.cmd` with an installed MSVC toolset and
CMake/Ninja. `py tools/build_recomp.py --data INSTALL --work OUTSIDE_REPO`
translates, builds, gathers route coverage and checks parity; it needs an
installed Python. Capstone adds optional disassembly comments; Pillow and
NumPy support the separate picture/sound comparison tools. The setup path
does not need those Python packages. A developer build without generated C
supports only the interpreter. After a runtime change, `--parity-only`
checks the existing generated build without translating or rebuilding.

## Running

Setup users start the game from the shortcut or the Start menu (or `f117a.exe` in the install folder). The commands below refer to a developer build.

```bat
build\f117a.exe --data "C:\GOG Games\F-117A"
```

The game asks its original SETUP questions at each start (joystick, sound
card), as it did in 1991. Answer 2 for AdLib, or 3 for Roland with `roland`
set (and, for Munt, `mt32-roms`).

**Game and fixes**

| Option | Meaning |
|---|---|
| `--data DIR` | the game's folder (found automatically for common GOG/Steam paths) |
| `--save DIR` | where the game's own saves go (default: `%APPDATA%\F117Recomp\F-117A\save`); the install is never written |
| `--engine recomp\|interp` | recompiled code (default) or the reference interpreter only |
| `--timing dosbox\|386` | the emulated PC's speed model: `dosbox` (default) is this machine's instruction model at `--ips`; `386` models a 1991 386DX/33 (original flight typically 5-9 fps with fixes off) |
| `--ips N` | emulated CPU speed under `dosbox` timing, instructions per second (60,000,000 in `f117a`; 9,000,000 in `f117run` and route checks, GOG DOSBox's `cycles=9000`). Default D1REAL separately targets fifteen simulation steps per second |
| `--sim-hz 8\|15` | the D1REAL simulation rate (below): 15 (default) or the earlier 8; `--sim-hz 8 --ips 20000000` is the previous play mode. Replays restore their recorded rate; logs without one use 8 |
| `--fixes original\|recommended` | the fix set as a single choice: `original` plays the shipped game with every fix off; `recommended` enables the thirty-three normal-play fixes (the default). Later `--fix ID` options make a custom set. The saves remember the set that last wrote them (a `.fixset` file beside the save folder), and the app warns once when a session switches between the original game and a fixed set on the same saves |
| `--fix ID`, `--list-fixes` | enable a fix; `all` enables the thirty-three normal-play fixes, `none` clears the set. Normal play starts with all thirty-three enabled; `--fix none --fix D5` selects only D5. `QUIZ` requires its own explicit selection. Listing prints the current selection |
| `--config FILE`, `--no-config` | read settings from FILE instead of `f117a.ini` beside the executable; or read none |

**Sound**

| Option | Meaning |
|---|---|
| `--roland munt\|windows\|off` | where Roland music goes: Munt's MT-32 emulation (needs `--mt32-roms`), the Windows MIDI synthesizer (General MIDI: wrong instruments), or nowhere |
| `--mt32-roms DIR` | the folder holding your MT-32 control and PCM ROMs, recognised by content whatever they are called (see [MT-32 ROMs](#mt-32-roms)); on its own it means `--roland munt` |
| `--midi N` | send Roland MIDI to Windows MIDI device N instead of the mapper |
| `--mt32-control FILE --mt32-pcm FILE` | name the two ROM files instead of a folder |
| `--opl dbopl\|nuked` | GOG DOSBox's OPL2 synthesizer (default), or Nuked OPL3 in OPL2 mode; both output at 44,100 Hz |
| `--speaker realsound\|pwm` | smoothed digitised speaker speech (default), or the original PWM carrier |
| `--sound ask\|adlib\|roland\|speaker` | the sound driver SETUP is answered with; `ask` (default) shows SETUP's own questions |
| `--adlib-effects`, `--no-adlib-effects` | with Roland music, whether the AdLib effects driver plays the effects beside it |
| `--mac-sounds DIR` | use the Macintosh recordings (`NNN.wav`) in DIR where the Mac version has one |
| `--music-source`, `--sound-source default\|mac\|dos` | which version's music or effects play: the best available (default), the Macintosh recordings, or the DOS drivers |
| `--mt32-seed N` | a fixed random seed for Munt, so the same input renders the same audio (needs `--mt32-control` and `--mt32-pcm`) |

**Picture**

| Option | Meaning |
|---|---|
| `--scale N`, `--fullscreen`, `--no-aspect` | window size; fullscreen; square pixels instead of 4:3 |
| `--presentation original\|enhanced\|remastered` | the picture as a single choice: `original` is the exact 320x200 picture, sharp, at 4:3 (the default); `enhanced` draws in-between frames at the display's refresh rate, as fine as the display's height allows at 4:3 (`--present rerun` with a matching `--present-scale`). A later `--present` option refines it. `remastered` redraws the 3-D view natively with a wide view, far scenery and smooth motion between the game's steps ([Presentation](#presentation)); it needs the Remastered renderer. |
| `--present scan\|replay\|interp\|rerun` | the picture in flight; see [Presentation](#presentation) |
| `--present-scale N` | with `replay`, `interp` or `rerun`: the picture N times finer, 1-16 |
| `--present-line-width N` | fine HUD stroke width in output pixels, 0-20; 0 (default) scales with `--present-scale`, 1 is a thin stroke; text and artwork keep their original pixels |
| `--present-age interp\|extrapolate` | with `interp` or `rerun`: trail the original by one logic step and be exact at its frames (default), or predict beyond the latest frame (can overshoot a sudden change) |
| `--present-log FILE` | write one line per displayed frame (host time, game clock, picture ages and the work per frame in nanoseconds) for timing |
| `--draw-distance maximum\|far\|near\|original\|N` | Remastered: how far scenery is drawn; `original` keeps the game's short reach, `maximum` draws every ring to the limit of the game's arithmetic, N is in world units. Farther costs picture rate |
| `--dot-distance original\|near\|far\|maximum` | Remastered: how far the small dark dots on plain ground are drawn (default `near`); lit points follow `--draw-distance` |
| `--model-detail full\|original` | Remastered: `full` (default) draws every model in its most detailed version at any distance, so buildings' lights no longer pop in; `original` chooses by distance as the game does |
| `--far-light-size original\|perspective` | Remastered: `original` draws far lights and lines one game pixel wide at any distance; `perspective` (default) shrinks those beyond the game's own reach with distance, down to one screen pixel |
| `--remaster-view wide\|4:3` | Remastered: `wide` (default) fills a wide window with a wider view; `4:3` keeps the original's framing, with black bars in a wide window. Either way the picture is redrawn without the original's road and coastline jitter |
| `--pick-order exact\|fast` | Remastered: the order far objects are sorted in. `exact` (default) is the order every picture comparison was made against; `fast` is cheaper by about 0.8 ms a frame at Maximum and lets objects tied on distance swap places, changing a few edge pixels in some frames |
| `--crt`, `--crt-mask`, `--crt-glow` | a CRT look: dark scanlines, a coloured phosphor mask, and a soft glow around bright parts (settings menu: Video) |

**Recording, replay, controls and accessibility**

| Option | Meaning |
|---|---|
| `--record FILE`, `--no-record`, `--replay FILE` | record this session's input to FILE, or record nothing; play a recorded input log back |
| `--watch latest\|SESSION`, `--seek SECONDS`, `--replay-fixes log\|cli` | play a recorded session again (from the sessions folder beside the save folder), starting this many seconds in; use the fix set recorded in the log, or this command line's |
| `--remap A:B` | swap two keys, in SDL key names (`Q:W`, `Left Shift`, `F5`, `Keypad 4`); repeatable. The Keys tab in the settings menu does the same |
| `--pad-deadzone N`, `--pad-fire1 N`, `--pad-fire2 N`, `--pad-invert-x`, `--pad-invert-y`, `--pad-keys B:KEY,...` | controller options: the stick dead zone (0 to 50), the fire buttons, axis inversion, and extra buttons sent as keys (for example `2:G,3:Left Shift`) |
| `--colourblind off\|protanopia\|deuteranopia\|tritanopia` | a colour-vision aid for the HUD colours |
| `--subtitles` | show radio calls as text so they can be read as well as heard |

Any option the app does not recognise (for example `--help`) prints a usage
summary. Diagnostic options not in the table: `--log FILE` (the machine log, ending with the final state hash),
`--exit-after CLOCKS` (stop at that instruction clock), `--time-us N` (the DOS clock's start, in microseconds),
`--coverage FILE` (record which instructions ran), `--audio-dump FILE` (the mixed audio) and `--audio-queue-log FILE` (the audio queue's fill).

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
| ` (tilde) | the menu: two tabs, Settings and Keys; pauses the game. Tab switches tab, ` or Esc closes it |
| Alt+Enter | toggle fullscreen |
| Ctrl+Alt+P | pause / resume |
| Tab or Ctrl+Alt+C | screenshot in live play (Tab with the menu closed): a PNG named `OpenNighthawk YYYY-MM-DD HH-MM-SS.png` in your Pictures folder, in `OpenNighthawk` (change it with `--screenshots DIR`, the `screenshots` ini key or Settings, Folders), with a brief on-screen message naming the file; a second picture in the same second gets ` (2)`. In a replay, Tab saves `screenshots/<session>-clock<flight clock>.bmp` beside `sessions` so the picture names the flight and moment, unless a screenshots folder is given |
| G, digits, Enter (watching a replay) | jump to that second |
| + / - / 0 (watching a replay) | faster / slower / normal speed |
| ] / [ (watching a replay) | jump 30 seconds on / back |
| Ctrl+Alt+R (watching a replay) | start the replay again |
| Ctrl+Alt+F12 | quit |

Esc is the game's own (it cancels the Alt+Q quit dialog in flight, skips the title animation, cancels a text entry), so it
only does anything in the menu while the menu is open. `Ctrl+Alt+S` and `Ctrl+Alt+K` still open the menu on the Settings and Keys tabs.

**Changing keys.** Choose *Remap a key...*, select a physical key, then select what it should send to the game. All 101 supported
game keys are listed; changed mappings appear in blue. Backspace restores one key, and Page Up/Down or Home/End moves through the list.
Assignments are live and saved immediately; *Clear key remaps* removes them all. The Keys tab lists the game's default commands.

**Settings pages.** Video, Audio and Advanced pages group the picture controls, the sound controls and the machine/simulation controls. Nondefault values are blue;
each setting has a description. *Reset all settings to default* takes effect when you Save and keeps game/sound folders.
See the [settings guide](docs/settings.md) for restart, reset and key-mapping behavior.

**Controllers and joysticks.** In the same menu: *Stick dead zone %*, *Invert stick X / Y*, *Fire 1 button* and *Fire 2 button* (0 south,
1 east, 2 west, 3 north, in SDL's gamepad order), and *Controller button as key...* (press a controller button, then the key it should act
as; *Clear controller keys* empties the list). The same in `f117a.ini`: `pad-deadzone`, `pad-invert-x`, `pad-invert-y`, `pad-fire1`,
`pad-fire2`, `pad-keys = 2:G,3:Left Shift`.

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

**Enhanced and Remastered are experimental.** *Original* (`--presentation original`) is the verified picture, exactly what the
emulated VGA scans out. Expect objects that appear or vanish at the edges of the view, depth and drawing glitches, and, unless you
raise Draw distance (Near, Far or Maximum, which costs picture rate), a draw distance no longer than the original's. They never
change the game itself, and reports of what looks wrong are welcome.

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
- `--presentation remastered` rebuilds the 3-D view with this project's own renderer from a render copy of the machine: a wide view in a widescreen window, depth-correct drawing, and
  far scenery out to the whole map (the *Draw distance*, *Terrain dots* and *Far object order* settings), drawn at the display's refresh rate. It is experimental. Remastered composes and presents the picture on the GPU
  (Windows with a Direct3D 12 GPU; the CPU compositor is used automatically without one, and `F117_GPU_COMPOSE=0` / `F117_GPU_PRESENT=0` turn the GPU path off): 4K frame times of 14.5 to 17.1 ms
  on the author's machine against 32 to 40 ms for the CPU path, which on a 4K flight fell to 7 frames a second and slowed the game; see [docs/gpu-compositor.md](docs/gpu-compositor.md) and [docs/remaster-performance.md](docs/remaster-performance.md).

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

For a 4K display, choose Remastered (or Enhanced) in setup or the ` menu: Picture detail `auto` fits the display's height (9 on 3840 x 2160, 4 on 1080p) and
setup asks whether to start in full screen. Select 3840x2160 at 60 Hz in Windows' display settings first. For the sharp mode without in-between frames,
use `--present replay --present-scale 9`.

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

You supply two ROM files from a Roland MT-32 (or CM-32L): the **control ROM** (the unit's firmware) and the
**PCM ROM** (its sampled sounds). Put them in one folder and point `mt32-roms` at it. The files are recognised by
their SHA-1, so their names do not matter. F-117A (1991) wants the first-generation MT-32 with a 1.0x control ROM;
2.0x and the CM-32L are accepted as fallbacks. The accepted ROMs, their SHA-1 values and how they were compared
are in [docs/mt32-roms.md](docs/mt32-roms.md).

## Verifying parity yourself

Host sound, moving rendering and shortcut behavior have a separate
[recorded-flight acceptance suite](docs/host-acceptance.md). Run it before
deploying host/presentation changes. It supplements the machine gates below;
its report cannot claim simulation parity or owner/GOG acceptance.

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
| `src/present/` | the optional sharper and smoother visuals (`replay`, `interp`, `rerun`) and the Remastered renderer with its optional GPU compositor |
| `recompiler/` | the translator (Python) |
| `third_party/` | DOSBox DBOPL and Nuked OPL3, the two OPL synthesizers |
| `tools/` | the build pipeline, unpacker, scripted routes, checks |
| `tests/` | silicon-vector harnesses for the interpreter and for generated code; lockstep and unit tests |
| `docs/` | [architecture](docs/architecture.md), [roadmap](docs/roadmap.md), [progress scoring](docs/progress.md), [original-game bugs](docs/bugs.md), [presentation](docs/presentation.md), [parity audit](docs/parity-audit.md), [repeated processes](docs/repeated-processes.md), [provenance](docs/provenance.md), [other versions](docs/other-versions.md) (Macintosh, Amiga), [MT-32 ROMs](docs/mt32-roms.md) |

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

- **Zig contributors**, for the MIT-licensed Zig compiler used by setup. Zig is downloaded at run time, not redistributed in source releases.

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
- **Ghidra** (the NSA Research Directorate and the Ghidra contributors), the decompiler
  used as a reading aid on the large routines; its output informs the reading only, and the
  lockstep check decides correctness.
- **debugcom** and **damsonn**, for the mission-generator and secret-airstrip analysis (debugcom) and the fan patch for it (packaged by damsonn) that the
  bug tracker cites.
- **GOG** and **Steam**, for keeping the game on sale.
