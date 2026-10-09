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
`src/io.c`, `src/vnc.c`, `src/qt/qt_main.cpp`, `src/sound/snd_adlib.c`, `src/cpu/386.c` and `src/video/vid_vga.c`) before step 5.
A traced run needs no VNC client, so it is a function of the machine and its
inputs, not of wall-clock polling. `trace_86box.ps1` starts it; environment
variables drive it:

| Variable | Effect |
|---|---|
| `B86_TRACE=DIR` | `DIR/frames.csv`: every displayed frame with its emulated microseconds, size, hash and the instructions the guest has executed so far (the patch counts them in the 386 interpreter) |
| `B86_KEYS="frame:down:scancode,..."` | key events at displayed-frame counts (set-1 scancodes, hex; an extended key is `e0xx`); up to 8,192 |
| `B86_MOUSE="frame:m:x,y;frame:b:mask;..."` | pointer to guest pixel x,y of 320x200 (a slam into the corner, then the move 30 frames later, fed 60 counts a frame; `B86_MOUSE_KX/KY` counts a pixel, default 0.667: under CuteMouse a count moves the guest pointer 1.5 pixels, under MOUSE.COM see the MS-DOS VMs below) and the button mask. The pointer is not at x,y until well after the `m` frame, so a press goes 40 frames after it and the release 55 (as `save_parity.py` does); a press at the route's own spacing clicks mid-move and does nothing |
| `B86_KEYS_FILE=FILE`, `B86_MOUSE_FILE=FILE` | the same schedules read from a file: a long route's inline string exceeds the Windows command-line and environment limits |
| `B86_PPM=1`, `B86_PPM_AFTER=N` | each new picture saved as a PPM (about 0.9-1.5 MB each), from frame N on if given |
| `B86_DUMP_FILE=FILE`, `B86_DUMP_AT=N` | conventional memory (0-0x9FFFF) written to FILE once at frame N |
| `B86_LOOP_STATE`, `B86_LOOP_REPLY`, `B86_LOOP_EVERY`, `B86_LOOP_READS` | the closed loop, as DOSBox-X's `DBX_LOOP_*` (build_dosbox_x.md), keyed to VGAME instead of a program name (below) |
| `B86_SEED_TICK=N` | START's mission generator restaged to seed N (below) |
| `B86_PORTLOG=FILE` | every write to the debug port 0xE9: the cycle counter at the start of the OUT, the instructions executed and the value (`probe386.py`) |
| `B86_STOP=N` | exit after N frames |
| `B86_OPL=FILE` | every AdLib register write as `microseconds register value` |
| `B86_SPKLOG=FILE` | every write to port 42h, 61h and a counter-2 command to 43h as `emulated-ms port value`, DOSBox-X's `DBX_SPEAKER_LOG` format (`speaker_parity.py`'s parser reads it) |
| `B86_FAST=1` | fast-forward: `pc_run()` back to back instead of one quantum per host millisecond; emulated time is still the TSC. `sound86.py` and `sav86.py` set it unless `--realtime` |

**Runs are deterministic** (8 Oct 2026). The harness's frame hook (key and mouse
injection, the closed loop, the seed staging, the frame hash and PPMs) runs in the
emulation thread as each frame is posted (`video.c`'s `b86_frame_hook`, set by
`vnc.c`), after the previous blit has finished, and hashes the emulated frame
buffer (`buffer32`). Before, it ran on the blit thread, so each injected input
landed a host-dependent moment later: two music runs differed by -10..+166 us in
AdLib timing and 7 of 12,000 frame hashes, and closed-loop flights parted from
the first input. Now two fast-forward music runs give byte-identical AdLib logs
(30,937 writes, times included), two intro captures byte-identical `frames.csv`
(13,020 frames, hash and TSC), and two closed-loop supply drops (1,531 ticks) and
two strike-training flights (2,696 ticks) byte-identical `flight.csv`; `pc_parity.py`
passes unchanged. Fast-forward: music 49 s instead of 178 s, save 95 s instead of
277 s. `probe86.py` has not been checked in fast-forward and does not set it.

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
Machine's recorded front end). Strike training (8 Oct 2026): the generated mission is the
Machine's (target 1 at 21200, 24272, the same loadout) and the primary is designated at about
3,050 map units; with the stick's own taps the deterministic flight misses (the frame taps below
make it hit): the lock breaks near 1,400 units (the nose
is left within the pilot's pitch and roll deadbands while the target leaves the seeker's view),
and the laser-guided bomb's hit window is narrow (bugs.md D7). Before runs were deterministic,
2 of 10 flights hit. Tighter deadbands and a wings-level release window were tried and made the
Machine and DOSBox-X flights miss, so they were not kept.

**The stick as whole frames** (8 Oct 2026). The game reads the stick once a frame and moves the
aircraft a frame's worth for each frame the key is down (`tools/stick_response.py`, isolated taps on
each machine): on the Machine and DOSBox-X (S 13-15 in that climb) a 60 ms tap is always one frame and
rolls the aircraft 596 units, 120 ms 1,192, 200 ms 2,384, identical on both; here (the FreeDOS VM, S 6-9) a 60 ms tap
is one frame of about 1,000 or none (-992, +5, -649). The pilots' small corrections therefore arrived
as random, oversized steps: the strike approach wandered 900 units off the bearing and the deck
approach rocked the wings to 22 degrees. The adaptor now sends the stick as whole game frames
(`_frame_tap`, on by default, `--no-frame-taps` to turn it off): the time asked for, scaled by
`STICK_SCALE` (1.3, this machine's response per ms over 86Box's), is kept per axis and sent as whole
frame periods from S, the remainder carried to the next press. With it the supply drop delivers, the
strike-training bomb hits and the career sortie lands (roster byte-identical to DOSBox-X's), and over
scales of 1.1, 1.3 and 1.5 seven of the nine flights pass: the supply drop at all three, the strike at
1.1 and 1.3 (it misses at 1.5), the career at 1.3 and 1.5 (at 1.1 the photo is taken outside the
camera's cue and not credited). Holding the arrow keys a fixed minimum (`--min-hold-ms`) was the first
try: it steadied the strike approach but no length passed all the flights.

**A career sortie** (`--pilot recon --debrief`, as `build_dosbox_x.md`'s, `--seed-tick 31233`): END's
keys go through the loop's replies from VGAME's end plus 1 s (86Box has no view of program starts),
including a click (the loop's `m x,y` and `b n` replies, added for it), and the run stops 90 s after
the last key, once START has written `ROSTER.FIL`, which is read back from the disk image. With the
stick's taps the landing pilot rocks the wings on the deck approach (left and right every two ticks, to
about 22 degrees of bank) and the aircraft crashes short of the raised deck: START records the pilot as
lost (status 2). With the stick in whole frames (above) it lands and stops, and the saved roster is
byte-identical to DOSBox-X's (mission score 167; ours scores 168, the flights differ) and passes
`career_check`. `replies.log` in the run directory holds
every reply with input. Its front end is
the route's START-to-VGAME events (2 keys, 11 clicks) on displayed frames:
START.EXE first appears at frame 10,775 on this profile (measured by memory
dumps, between 10,700 and 10,850), and later events follow at 70.086 frames
per second of the Machine's clock. Over that span 86Box's own clock (274 s)
agrees with DOSBox-X's (268 s) to 2%.

Its users: `probe86.py` (the machine-behaviour probe; answers are read back out
of the disk image), `sound86.py` with `compare_opl86.py` (the music) and
`sav86.py` (a scripted START session and its saved `ROSTER.FIL`;
`tools/save_parity.py` turns a route file into the schedule).

**A mouse needs a DOS driver, and on the FreeDOS VM the game needs almost all of
conventional memory.** The default MS-DOS VM loads its own MOUSE.COM. On FreeDOS `sav86.py` puts CuteMouse's `ctmouse.exe` (GPL; a built copy is in
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
286 2.45 s, the 386DX/33 1.8-2.0 s (scene lengths within 0.31 s). Where it
builds (8 Oct 2026, per paired scene): +0.57 s between the scenes at 3.9 and 13.0 s
(the title pictures load) and +0.80 s between 35.8 and 45.6 s (`credit.PAN` loads at
41.0 s), then a few hundredths a scene. The disk path is part of the first step only: a
16-bit AT IDE controller (`hdc_1 = ide_isa`, the drive written into the CMOS as type
47, 940/8/17) and 20 DOS buffers (`B86_BUFFERS=20` for `probe86.bare_boot`) take the
drift from 1.98 s to 1.74 s and the first step to +0.31 s. A 40 MHz CPU takes the first
step to +0.47 s and leaves the second at +0.80 s. The second is video memory: the
profile's IBM VGA costs 8 ISA bus clocks a byte (86Box's `timing_vga`), and the credits'
panning pictures write the screen every frame; with `B86_VGA_FAST=1` (a test knob in
`vid_vga.c`: one bus clock a byte) the step is gone (+0.51 s before and after it) and the
drift over the whole intro is 0.56 s, the title-picture step. The intro music lags by
the same amounts, since the game loop advances it. So the drift is the reference PC's
own disk and video speed, which this machine (and DOSBox) do not charge; it is not an
error in this machine. The routine check keeps the stock card (a period PC). All traced runs use `probe86.bare_boot` (on the FreeDOS
VM the game needs almost all of conventional memory under this BIOS; an MS-DOS VM keeps its own boot files).

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
to ours. Checked 8 Oct 2026: with `B86_VGA_FAST=1` channel 3's key-ons go from 8,986
to 9,022 (ours 8,982; 9,022 on the MS-DOS 5.00 VM without it) while channels 0, 1, 2 and 5 stay identical, so the busy channels'
writes follow the game loop's speed on the machine that runs it, not a fixed schedule;
no single fixed PC speed reproduces them, and they are not a parity target.

**MS-DOS reference VMs.** `build_msdos_vm.py --disks DIR --out D:\86box\vmt386dosNNN`
builds vmt386's board with a new disk booting the owner's MS-DOS (4.01, 5.00 and
6.22 are built: `vmt386dos401`, `vmt386dos500`, `vmt386dos622`): what `SYS C:` does,
written by the script (the system disk's boot code, IO.SYS and MSDOS.SYS first and
contiguous, COMMAND.COM), the MBR code of the FreeDOS VM's disk, a first boot that
proves the disk boots, then the game and `FILES=20`, `BUFFERS=20`, and a boot that
writes `MEM /C` into the profile's `MEM.TXT`. The board has 1 MB, so no extended
memory and no HIMEM; `--mem-kb N --himem` gives it N KB (the first boot saves the
size into the CMOS through the BIOS setup) and loads MS-DOS's HIMEM.SYS with
`DOS=HIGH`. MS-DOS 5.00 is the 386 timing profile's reference
([timing386.md](timing386.md)); `vmt386dos500` loads Microsoft MOUSE.COM 6.26
(`--mouse`, `mouse_type = msserial`) and is every 86Box tool's default
(`probe86.REFERENCE`, 9 Oct 2026; FreeDOS `vmt386` by `--profile` or
`--profile86`). Largest executable program under its
AUTOEXEC.BAT: 577,728 bytes; `vmt386dos500h` (`--mem-kb 4096 --himem`, the same
mouse) 625,584. Both load SPEECH.117 in flight; the FreeDOS bare boot does not.

On an MS-DOS VM `probe86.bare_boot` keeps the boot files as built (its mouse
driver, then F117), and `b86_cargo_pilot.py --profile`, `save_parity.py
--profile86` and `pc_parity.py --profile86` run on it. MOUSE.COM moves the
pointer one pixel a count (`B86_MOUSE_KX/KY` 1.0, against CuteMouse's 0.667) and
stops 8 pixels short in x and 16 in y after a move from the corner (eight moves
on START's roster screen, each exactly that short); `probe86.mouse_calibration`
gives the harnesses both from the image. With it the cargo pilot reaches VGAME
and the roster check saves a `ROSTER.FIL` byte-identical to ours on
`vmt386dos500h`; without it the clicks land 8-16 pixels off and START never
leaves the roster.
`b86_cargo_pilot.py` answers SETUP with the front route's own two keys (every
recorded route answers N and 2, AdLib; a route answering 1 flies with the speaker
driver).

## The profile (`D:\86box\vmf`, the picture capture; the checks above use `vmt386dos500`)

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
83 close, 0 unmatched, order preserved (since 8 Oct 2026 the comparison is in
6-bit DAC values, since 86Box expands them as floor(v*255/63) and this machine
as v<<2|v>>4: 86 exact, 1 close, the close one a single sample taken
mid-draw); two runs gave the same 88 pictures at
the same times; 48 s instead of about 3 min. That was the FreeDOS VM. On the
MS-DOS 5.00 VM, the default since 9 Oct 2026: 87 distinct pictures, 86 compared,
85 exact and one unmatched, p085, START's roster screen sampled while 86Box is
still loading the selected pilot's panel (this machine loads files at once and
never shows that state; listed in `expected_misses86.txt`).

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
