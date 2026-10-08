# Running the original under 86Box, headless

The release 86Box has no scripted input and always opens a window. A source
build with the VNC renderer serves the guest display over VNC and takes key
events from it, so the machine runs with **no window, no sound and no
interference from the user's desktop**, and a script can type and capture
frames. Verified 6 Oct 2026: the original's SETUP screen, MicroProse logo,
intro, credits and sound menu on `ami286` (a 6 MHz 286).

## Build (once; about 5 minutes after the downloads)

1. MSYS2: `curl -L -o msys2.sfx.exe https://repo.msys2.org/distrib/msys2-x86_64-latest.sfx.exe`,
   run it with `-oD:/` (installs `D:\msys64`). The first `pacman -Syu` closes
   its own shell; run it twice.
2. Packages (UCRT64): git, toolchain, cmake, ninja, qt6-base, qt6-tools,
   qt6-svg, SDL2, openal, freetype, libpng, rtmidi, fluidsynth,
   libserialport, libslirp, pkgconf, vulkan-headers, extra-cmake-modules,
   libvncserver. (`openmp` is part of the toolchain, not a package.)
3. `git clone --branch v6.0 https://github.com/86Box/86Box.git` to
   `D:\86box-src\src`.
4. One local edit, needed with current MinGW headers: in `src/cpu/808x.c`,
   after the last `#include`, add `#define access cpu_808x_access` (the
   file defines its own `access()`, which clashes with `io.h`).
5. In an MSYS2 UCRT64 shell: `cmake -S src -B build -G Ninja
   -DCMAKE_BUILD_TYPE=Release -DVNC=ON -DQT=ON -DUSE_QT6=ON -DDEV_BRANCH=OFF
   -DNEW_DYNAREC=OFF -DDYNAREC=ON` then `ninja -C build`. Output:
   `build/src/86Box.exe`.

## Traced runs: emulated time, scripted input (`86box-trace.patch`)

`git apply tools/ref86box/86box-trace.patch` in the checkout (it edits
`src/vnc.c`, `src/qt/qt_main.cpp`, `src/sound/snd_adlib.c` and `src/cpu/386.c`) before step 5.
A traced run needs no VNC client, so it is a function of the machine and its
inputs, not of wall-clock polling. `trace_86box.ps1` starts it; environment
variables drive it:

| Variable | Effect |
|---|---|
| `B86_TRACE=DIR` | `DIR/frames.csv`: every displayed frame with its emulated microseconds, size, hash and the instructions the guest has executed so far (the patch counts them in the 386 interpreter) |
| `B86_KEYS="frame:down:scancode,..."` | key events at displayed-frame counts (set-1 scancodes, hex; an extended key is `e0xx`); up to 8,192 |
| `B86_MOUSE="frame:m:x,y;frame:b:mask;..."` | pointer to guest pixel x,y of 320x200 (a slam into the corner, then the move 30 frames later, fed 60 counts a frame; a count moves the guest pointer 1.5 pixels) and the button mask. The pointer is not at x,y until well after the `m` frame, so a press goes 40 frames after it and the release 55 (as `save_parity.py` does); a press at the route's own spacing clicks mid-move and does nothing |
| `B86_KEYS_FILE=FILE`, `B86_MOUSE_FILE=FILE` | the same schedules read from a file: a long route's inline string exceeds the Windows command-line and environment limits |
| `B86_PPM=1`, `B86_PPM_AFTER=N` | each new picture saved as a PPM (about 0.9-1.5 MB each), from frame N on if given |
| `B86_DUMP_FILE=FILE`, `B86_DUMP_AT=N` | conventional memory (0-0x9FFFF) written to FILE once at frame N |
| `B86_LOOP_STATE`, `B86_LOOP_REPLY`, `B86_LOOP_EVERY`, `B86_LOOP_READS` | the closed loop, as DOSBox-X's `DBX_LOOP_*` (build_dosbox_x.md), keyed to VGAME instead of a program name (below) |
| `B86_SEED_TICK=N` | START's mission generator restaged to seed N (below) |
| `B86_STOP=N` | exit after N frames |
| `B86_OPL=FILE` | every AdLib register write as `microseconds register value` |
| `B86_FAST=1` | fast-forward: `pc_run()` back to back instead of one quantum per host millisecond; emulated time is still the TSC. `sound86.py` and `sav86.py` set it unless `--realtime` |

86Box is not bit-deterministic between runs, even at real time: two real-time
music runs gave the same 30,937 AdLib writes in the same order with timing
spread over -10..+166 us, and 7 of 12,000 frame hashes differed (`frames.csv`
is sampled when the blit thread hashes the screen). Fast-forward stays inside
that: the same writes in the same order (-201..+10 us against the two real-time
runs), 20-23 frame hashes differing, the first frame logged 1.7 ms later (the
blit thread lags a fast emulation), the saved roster identical; music run 49 s
instead of 178 s, save run 95 s instead of 277 s (7 Oct 2026). `probe86.py` has
not been checked in fast-forward and does not set it.

### Closed loop (B86_LOOP_*) and the seed (B86_SEED_TICK)

86Box knows nothing of DOS, so it cannot name the running program or its PSP
the way DOSBox-X does. VGAME is found by its own bytes instead: a 48-byte run
of its loaded image (file offset 0xE6CE, by its entry point, no relocation in
it) is searched for at every paragraph of conventional memory each frame; at
load segment L, VGAME's DS is L + 0x1E42, the Machine's own `PSP + 0x10 +
0x1E42`, so the read sets are the same. From then on, every `B86_LOOP_EVERY`
emulated ms (200 by default) the emulator appends `seq ms max=9000 left=0
cpu=0 seg=L live=0|1` and the DS-relative bytes of `B86_LOOP_READS` (up to
1,024 ranges) to `B86_LOOP_STATE`, and blocks, with emulated time stopped,
until `B86_LOOP_REPLY<seq>` exists. A reply is `ms|r|hexbyte;...`: raw XT
scancode bytes (0xE0 sent as its own item) at that many ms after the tick, as
`key_bytes()` in `dosbox_cargo_pilot.py` writes them; `stop` exits 86Box.
`live` is 0 once a second VGAME run of bytes (offset 0x10, near the image
start) no longer matches: DSWAP and END load over it, and the data segment
then holds their bytes, not VGAME's.

START seeds its mission generator (state at its DS:AE8C, DS = load segment +
0xA95) with srand(BIOS tick) as `requestr.pic`'s decode ends. 86Box runs real
BIOS ROM code, so the INT 1Ah call cannot be trapped as `DBX_INT1A_TICK` does,
and poking the tick count is off by a few ticks (a different mission each
run); freezing it for START's whole session stopped the menus responding.
`B86_SEED_TICK=N` restages the generator instead: START is found by its own
signature (file offset 0x8EEC), and the first frame its state leaves the C
runtime's `srand(1)`, the seed tick and step count that produced it are found
by search and the state is replaced by srand(N) stepped as many times.
`TRACE/seed.txt` records it. Checked (7 Oct 2026): 86Box's own tick was 3380,
caught 0 steps in, restaged to 31579; VGAME's mission bytes (objective 3,
target 24, secondary 1) then match the Machine's.

`tools/b86_cargo_pilot.py` flies `cargo_pilot.py` this way, or with `--pilot strike` the strike-training
pilot (`--front-end-clock 2761292060 --start-exec-clock 956971536 --seed-tick 31324` and the
Machine's recorded front end). Strike training (8 Oct 2026, ten flights): the generated mission
is the Machine's in all ten (target 1 at 21200, 24272, the same loadout) and the primary is
designated at about 3,050 map units in all ten, but only 2 of 10 hit: on this track the lock
breaks near 1,400 units in most flights (the nose is left within the pilot's pitch and roll
deadbands while the target leaves the seeker's view), and the laser-guided bomb's hit window
is narrow (bugs.md D7). Tighter deadbands and a wings-level release window were tried and
made the Machine and DOSBox-X flights miss, so they were not kept. Its front end is
the route's START-to-VGAME events (2 keys, 11 clicks) on displayed frames:
START.EXE first appears at frame 10,775 on this profile (measured by memory
dumps, between 10,700 and 10,850), and later events follow at 70.086 frames
per second of the Machine's clock. Over that span 86Box's own clock (274 s)
agrees with DOSBox-X's (268 s) to 2%.

Its users: `probe86.py` (the machine-behaviour probe; answers are read back out
of the disk image), `sound86.py` with `compare_opl86.py` (the music) and
`sav86.py` (a scripted START session and its saved `ROSTER.FIL`;
`tools/save_parity.py` turns a route file into the schedule).

**A mouse needs a DOS driver, and the game needs almost all of conventional
memory.** `sav86.py` puts CuteMouse's `ctmouse.exe` (GPL; a built copy is in
github.com/davidebreso/ctmouse; not kept in this repository, default path
`D:\f117-gate\ctm\CTMOUSE.EXE`) on the disk, sets `mouse_type = msserial` and
boots a bare FreeDOS (no environment variables, `BUFFERS=1`, `FILES=10`,
`/E:128`): CTMOUSE's 3 KB otherwise makes START stop with "Not enough memory!".

**The parity checks run on a 386DX/33, not the 286.** `make_profile386.py`
builds `D:\86box\vmt386` from the 286 profile: board `ami495` (OPTi 495SX, AMI
BIOS; `opt495sx.ami` is fetched from the 86Box roms repository), i386DX at 33 MHz,
1 MB, and a CMOS made by driving the BIOS setup with injected keys ("auto
configuration with BIOS defaults", F10, Y) and keeping the NVR 86Box saves on
exit. Scene timing against this machine, from the traced `frames.csv`
(`compare_timing86.py`): the 6 MHz 286 drifted 8.35 s over the intro, a 25 MHz
286 2.45 s, the 386DX/33 1.8-2.0 s (scene lengths within 0.31 s); 40 MHz
changes it by 0.1 s, so what remains is paced by the disk interface (ISA port
I/O), not the CPU. All traced runs use `probe86.bare_boot` (the game needs
almost all of conventional memory under this BIOS).

**Instructions against this model's clocks** (`instr86.py`, from the same frames.csv and a
`video_compare.py` run): between the intro's paired scenes the game's code runs at 6.28 MIPS
on the 386DX/33 while this model runs 9.00 M clocks a second: 0.712 instructions for each
clock, steady (0.70-0.73) in all 18 intervals. The intro is paced by timers, so both machines
spend the same time in each interval and most of it in busy-wait loops (the retrace poll at
0x3DA is read 456 M times over the 32 routes); the ratio therefore compares how fast a polling
loop turns, not general CPU speed: about 0.48 us an iteration on 86Box (an ISA read costing
roughly four 8.33 MHz bus clocks) against 1.2 us here (DOSBox's 8-clock read delay at 9 MHz).
It cannot calibrate the model for CPU-bound work; that needs a stretch with no polling.

Measured 7 Oct 2026 on it: the music's first 2,000 AdLib writes are identical to
this machine's, channels 0, 1, 2 and 5 play the same notes in the same order,
and channels 3 and 4 have the same number of key-ons (within 0.1%) but their
pitch-bend writes fall in different places (the effect is paced by the
loop, so by speed); the scripted session saves a `ROSTER.FIL` byte-identical
to ours.

## The profile (`D:\86box\vmf`, the picture capture; the checks above use `vmt386`)

`86box.cfg.vnc` here is the working configuration: `ami286`, 640 KB,
`hdc_1 = xtide_at`, VGA, AdLib, `vid_renderer = vnc`, `sound_muted = 1`, no
floppy. The disk image comes from `build_hdd.py` then `patch_hd_boot.py`
(FreeDOS plus the GOG files in `C:\F117A`); `nvr/ami286.nvr` from
`cmos_ami286.py`; `roms` is a junction to the release's ROM folder.
If a floppy is configured the BIOS boots it first.

## Run

    powershell -File tools\ref86box\start_vnc86.ps1 -Profile D:\86box\vmf
    py tools\ref86box\vnc86.py "wait 55; type f117; key Return; wait 10; shot a.png"

`start_vnc86.ps1` uses Qt's offscreen platform, below-normal priority, and
OpenAL's null device. While no client is connected the machine is paused;
`vnc86.py` words: `wait`, `shot`, `type`, `key`, `click`. The first run needs
about 55 s of real time to reach the DOS prompt. Stop with
`Stop-Process -Name 86Box`.

## What did not work (so nobody retries it)

PostMessage key events to the release build's window, with or without focus,
including from a driver running on a separate Windows desktop
(`hidden86.ps1`); the window does capture invisibly that way, but cannot be
typed into. Floppy-first boot order, an extended-memory size in CMOS the VM
does not have, a zero BPB geometry and an MBR without boot code each
stopped the boot before DOS.

## The check

Since 7 Oct 2026 `capture_intro.py OUT` is traced: the 386 profile in
fast-forward, SETUP answered at frames 3000 and 3300, and the picture on
screen sampled once per emulated second from frame 3300 (`--vnc` keeps the
real-time capture below). Result: 88 distinct pictures, 87 compared, 4 exact,
83 close, 0 unmatched, order preserved; two runs gave the same 88 pictures at
the same times; 48 s instead of about 3 min.

First result (6 Oct 2026, the VNC capture):

`capture_intro.py OUT` boots the VM, runs F117, answers SETUP and samples the
display once a second for 150 s; `compare_intro.py OUT SHOTS` matches each
86Box picture to the nearest of this machine's pictures (the `shots/` of a
`video_compare.py` run) in order. Run against `intro-n82oys6t`:

- 105 distinct pictures on 86Box; 104 are within 3 levels per channel of a
  picture of ours, none byte-exact, order preserved with no backward match.
  The one unmatched picture is the first sample (SETUP's text screen has no
  counterpart in the graphics-only comparison).
- Both machines expand the VGA DAC identically (64 levels in steps of 4).
  The residual difference is the animation instant: a 1 Hz sample on a
  6 MHz 286 lands on a different frame of the same scene than ours does.
  So the scenes, their order and the colours agree; frame-exact timing
  does not and cannot until the VM runs at the modelled speed.
