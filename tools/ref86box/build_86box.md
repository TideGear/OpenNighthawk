# Running the original under 86Box, headless

The release 86Box has no scripted input and always opens a window. A source
build with the VNC renderer and this directory's patches runs the machine
with **no window, no sound and no interference from the user's desktop**,
types and clicks from a script, and records every frame, AdLib write and
speaker write in emulated time. It runs the original on a 386DX/33 with
MS-DOS 5.00, the reference for the 386 timing profile
([timing386.md](timing386.md)).

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
5. `git apply tools/ref86box/86box-trace.patch` in the checkout (it edits
   `src/cpu/386.c`, `src/io.c`, `src/qt/qt_main.cpp`, `src/sound/snd_adlib.c`,
   `src/video/vid_vga.c`, `src/video/video.c` and `src/vnc.c`), and
   `86box-audio.patch` for the speech capture (below).
6. In an MSYS2 UCRT64 shell: `cmake -S src -B build -G Ninja
   -DCMAKE_BUILD_TYPE=Release -DVNC=ON -DQT=ON -DUSE_QT6=ON -DDEV_BRANCH=OFF
   -DNEW_DYNAREC=OFF -DDYNAREC=ON` then `ninja -C build`. Output:
   `D:\86box-src\build\src\86Box.exe`, where the tools look for it. An
   incremental rebuild takes about 23 s.

## Traced runs: emulated time, scripted input (`86box-trace.patch`)

A traced run needs no VNC client, so it is a function of the machine and its
inputs, not of wall-clock polling. `trace_86box.ps1` starts it; environment
variables drive it:

| Variable | Effect |
|---|---|
| `B86_TRACE=DIR` | `DIR/frames.csv`: every displayed frame with its emulated microseconds, size, hash and the instructions the guest has executed so far (the patch counts them in the 386 interpreter) |
| `B86_KEYS="frame:down:scancode,..."` | key events at displayed-frame counts (set-1 scancodes, hex; an extended key is `e0xx`); up to 8,192 |
| `B86_MOUSE="frame:m:x,y;frame:b:mask;..."` | pointer to guest pixel x,y of 320x200 (a slam into the corner, then the move 30 frames later, fed 60 counts a frame) and the button mask. `B86_MOUSE_KX/KY` is counts per guest pixel: 0.667 (the default) for CuteMouse, 1.0 for Microsoft MOUSE.COM. The pointer is not at x,y until well after the `m` frame, so press 40 frames after it and release at 55 (as `save_parity.py` does); a press at the route's own spacing clicks mid-move and does nothing |
| `B86_KEYS_FILE=FILE`, `B86_MOUSE_FILE=FILE` | the same schedules read from a file: a long route's inline string exceeds the Windows command-line and environment limits |
| `B86_PPM=1`, `B86_PPM_AFTER=N` | each new picture saved as a PPM (about 0.9-1.5 MB each), from frame N on if given |
| `B86_DUMP_FILE=FILE`, `B86_DUMP_AT=N` | conventional memory (0-0x9FFFF) written to FILE once at frame N |
| `B86_LOOP_STATE`, `B86_LOOP_REPLY`, `B86_LOOP_EVERY`, `B86_LOOP_READS` | the closed loop, as DOSBox-X's `DBX_LOOP_*` ([build_dosbox_x.md](build_dosbox_x.md)), keyed to VGAME instead of a program name (below) |
| `B86_SEED_TICK=N` | START's mission generator restaged to seed N (below) |
| `B86_PORTLOG=FILE` | every write to the debug port 0xE9: the cycle counter at the start of the OUT, the instructions executed and the value (`probe386.py`) |
| `B86_STOP=N` | exit after N frames |
| `B86_OPL=FILE` | every AdLib register write as `microseconds register value` |
| `B86_SPKLOG=FILE` | every write to port 42h, 61h and a counter-2 command to 43h as `emulated-ms port value`, DOSBox-X's `DBX_SPEAKER_LOG` format (`speaker_parity.py`'s parser reads it) |
| `B86_FAST=1` | fast-forward: `pc_run()` back to back instead of one quantum per host millisecond; emulated time is still the TSC. `sound86.py`, `sav86.py`, `capture_intro.py` and `b86_cargo_pilot.py` set it unless `--realtime`, `frames386.py` always; `probe86.py` does not |
| `B86_VGA_FAST=1` | a test switch: video memory at one bus clock a byte instead of eight (below) |

`probe86.bare_boot` also reads `B86_BUFFERS=N`, the `BUFFERS=` it writes into
a FreeDOS VM's boot files (default 1).

**Runs are deterministic.** The frame hook (key and mouse injection, the
closed loop, the seed staging, the frame hash and PPMs) runs in the
emulation thread as each frame is posted (`b86_frame_hook` in `video.c`, set
by `vnc.c`) and hashes the emulated frame buffer. Two fast-forward music runs
give byte-identical AdLib logs (30,937 writes, times included), two intro
captures byte-identical `frames.csv` (13,020 frames), and two closed-loop
flights byte-identical `flight.csv`. The hook must stay on the emulation
thread: on the blit thread each input lands a host-dependent moment later
and closed-loop flights part from the first input. Fast-forward: music 49 s
instead of 178 s, save 95 s instead of 277 s.

The traced build's users: `probe86.py` (the machine-behaviour probe; answers
are read back out of the disk image), `sound86.py` with `compare_opl86.py`
(the music), `sav86.py` (a scripted START session and its saved
`ROSTER.FIL`; `tools/save_parity.py` turns a route file into the schedule),
`capture_intro.py`, `frames386.py`, `probe386.py` and `b86_cargo_pilot.py`.

### Closed loop (B86_LOOP_*) and the seed (B86_SEED_TICK)

86Box knows nothing of DOS, so it cannot name the running program or its PSP
as DOSBox-X does. VGAME is found by its own bytes: a 48-byte run of its
loaded image (file offset 0xE6CE, no relocation in it) is searched for at
every paragraph of conventional memory each frame; at load segment L, VGAME's
DS is L + 0x1E42 (the Machine's `PSP + 0x10 + 0x1E42`), so the read sets are
the same. From then on, every `B86_LOOP_EVERY` emulated ms (default 200) the
emulator appends `seq ms max=9000 left=0 cpu=0 seg=L live=0|1` and the
DS-relative bytes of `B86_LOOP_READS` (up to 1,024 ranges) to
`B86_LOOP_STATE`, and blocks, with emulated time stopped, until
`B86_LOOP_REPLY<seq>` exists. A reply is `ms|r|hexbyte;...`: raw XT scancode
bytes (0xE0 as its own item) at that many ms after the tick, as `key_bytes()`
in `dosbox_cargo_pilot.py` writes them; `m x,y` and `b n` replies move and
click the mouse; `stop` exits 86Box. `live` is 0 once a second run of VGAME
bytes (offset 0x10) no longer matches: DSWAP and END load over it.

START seeds its mission generator (state at its DS:AE8C, DS = load segment +
0xA95) with srand(BIOS tick) as `requestr.pic`'s decode ends. 86Box runs real
BIOS code, so the INT 1Ah call cannot be trapped as DOSBox-X's
`DBX_INT1A_TICK` does, and poking the tick count is off by a few ticks (a
different mission each run). `B86_SEED_TICK=N` restages the generator
instead: START is found by its signature (file offset 0x8EEC), and at the
first frame its state leaves the C runtime's `srand(1)`, the seed tick and
step count that produced it are found by search and the state is replaced by
srand(N) stepped as many times. `TRACE/seed.txt` records it. Checked: VGAME's
mission bytes then match the Machine's.

**The pilots** (`tools/b86_cargo_pilot.py`, default profile `vmt386dos500`,
`--profile` for another). `--pilot cargo` (the default) flies
`cargo_pilot.py`'s supply drop; `--pilot strike` the strike-training pilot
(`--front-end-clock 2761292060 --start-exec-clock 956971536 --seed-tick
31324` and the Machine's recorded front end); `--pilot recon --debrief` a
career sortie as in [build_dosbox_x.md](build_dosbox_x.md) (`--seed-tick
31233`). SETUP is answered with the front route's own two keys. The front
end's events go on displayed frames: START.EXE is taken to appear at frame
10,775 (`--frame-start-exec`; measured on the FreeDOS VM, and the MS-DOS 5.00
VM falls within that measurement's margin), later events follow at 70.086
frames per emulated second; over that span 86Box's clock agrees with
DOSBox-X's to 2%. For the career sortie END's keys go through the loop's
replies from VGAME's end plus 1 s, and the run stops 90 s after the last key,
once START has written `ROSTER.FIL`, which is read back from the disk image;
`replies.log` in the run directory holds every reply with input.

**The stick as whole frames.** The game reads the stick once a frame and
moves the aircraft a frame's worth for each frame the key is down. On the
Machine and DOSBox-X (S 13-15) a 60 ms tap is always one frame (596 units of
roll); at 86Box's S 6-9 it is one frame of about 1,000 units or none, so the
pilots' small corrections arrived as random, oversized steps. The adaptor
therefore sends stick time as whole game frames (`_frame_tap`, default;
`--no-frame-taps` turns it off), scaled by `STICK_SCALE` (1.3, `--stick-scale`)
and with the remainder carried to the next press. With it the supply drop
delivers, the strike-training bomb hits and the career sortie lands, its
saved roster byte-identical to DOSBox-X's (mission score 167; ours scores
168 because the flights differ) and passing `career_check`; over scales 1.1,
1.3 and 1.5 seven of nine flights pass. A fixed minimum key hold
(`--min-hold-ms`) and tighter pilot deadbands were tried and did not pass all
flights.

**A mouse needs a DOS driver.** The MS-DOS VMs load Microsoft MOUSE.COM.
On the FreeDOS VM, `sav86.py` puts CuteMouse's `ctmouse.exe` (GPL; build from
github.com/davidebreso/ctmouse; not kept in this repository, default path
`D:\f117-gate\ctm\CTMOUSE.EXE`) on the disk, sets `mouse_type = msserial` and
boots a bare FreeDOS (no environment variables, `BUFFERS=1`, `FILES=10`,
`/E:128`): the game needs almost all of conventional memory, and CTMOUSE's
3 KB otherwise makes START stop with "Not enough memory!".

## The reference machines

**The 386DX/33** (`make_profile386.py`, `D:\86box\vmt386` from the 286
profile): board `ami495` (OPTi 495SX, AMI BIOS; `opt495sx.ami` from the 86Box
roms repository), i386DX at 33 MHz, 1 MB, IBM VGA, AdLib, XT-IDE disk, and a
CMOS made by driving the BIOS setup with injected keys ("auto configuration
with BIOS defaults", F10, Y) and keeping the NVR 86Box saves on exit. The
drive uses 86Box's `1989_3500rpm` preset (`hdd_01_speed`, set by hand in
`vmt386` and `vmt386dos500`; no script writes it, and a VM built from
`vmt386` inherits it). The other MS-DOS VMs (`vmt386dos401`,
`vmt386dos500h`, `vmt386dos622`) still use `ramdisk`.

It was chosen over the 286 profiles by scene timing against this machine's
DOSBox model (`compare_timing86.py`): the intro drifted 8.35 s on the 6 MHz
286, 2.45 s on a 25 MHz 286 and 1.8-2.0 s on the 386DX/33. That residual
drift is the reference PC's own disk, DOS and video-memory speed, not a CPU
difference. It builds in two steps: +0.57 s while the title pictures load
(a 16-bit IDE controller and 20 DOS buffers cut it to +0.31 s) and +0.80 s
where `credit.PAN` loads and the credits' panning pictures write video memory
every frame (IBM VGA: 8 ISA bus clocks a byte; with `B86_VGA_FAST=1` that step
is gone and the whole intro drifts 0.56 s). The routine
checks keep the stock card, a period PC. The 386 timing profile charges these
costs ([timing386.md](timing386.md)).

`instr86.py` (from the same `frames.csv` and a `video_compare.py` run): over
the intro's paired scenes the game runs 0.712 instructions per DOSBox-model
clock (0.70-0.73 in all 18 intervals). The intro is paced by timers and
spends most of each interval in busy-wait loops (the retrace poll at 0x3DA),
so this compares how fast a polling loop turns (about 0.48 us an iteration on
86Box, 1.2 us under DOSBox's 8-clock read delay at 9 MHz), not general CPU
speed; calibrating CPU-bound work needs a stretch with no polling.

**MS-DOS reference VMs.** `build_msdos_vm.py --disks DIR --out
D:\86box\vmt386dosNNN` builds vmt386's board with a new disk booting the
owner's MS-DOS (4.01, 5.00 and 6.22 are built: `vmt386dos401`,
`vmt386dos500`, `vmt386dos622`): what `SYS C:` does (the system disk's boot
code, IO.SYS and MSDOS.SYS first and contiguous, COMMAND.COM), the MBR code of
the FreeDOS VM's disk, a first boot that proves the disk boots, then the game
with `FILES=20`, `BUFFERS=20`, and a boot that writes `MEM /C` into the
profile's `MEM.TXT`. The board has 1 MB, so no extended memory and no HIMEM;
`--mem-kb N --himem` gives it N KB (the first boot saves the size into the
CMOS through the BIOS setup) and loads MS-DOS's HIMEM.SYS with `DOS=HIGH`.
`--mouse FILE` loads a mouse driver with 86Box's serial mouse on COM1.

`vmt386dos500` (MS-DOS 5.00 with Microsoft MOUSE.COM 6.26) is the reference
and every 86Box tool's default (`probe86.REFERENCE`); `--profile` (or
`--profile86` for `save_parity.py` and `pc_parity.py`) selects another, such
as the FreeDOS `vmt386`. Largest executable program: 577,728 bytes;
`vmt386dos500h` (`--mem-kb 4096 --himem`, the same mouse) 625,584. Both load
SPEECH.117 in flight; the FreeDOS bare boot does not. On an MS-DOS VM
`probe86.bare_boot` keeps the boot files as built (its mouse driver, then
F117). MOUSE.COM moves the pointer one pixel a count and stops 8 pixels short
in x and 16 in y after a move from the corner; `probe86.mouse_calibration`
gives the harnesses both from the image. Without that correction the clicks
land 8-16 pixels off and START never leaves the roster.

## Music and saves on the reference

The music's first 2,000 AdLib writes are identical to this machine's;
channels 0, 1, 2 and 5 play the same notes in the same order; channels 3 and
4 have the same number of key-ons (within 0.1%) but their pitch-bend writes
fall in different places. That effect is paced by the game loop, so it
follows the speed of the machine that runs it (channel 3's key-ons change
with `B86_VGA_FAST=1`); no fixed PC speed reproduces it, and it is not a
parity target. The scripted START session saves a `ROSTER.FIL`
byte-identical to ours.

## The intro check (`capture_intro.py`)

`capture_intro.py OUT` is traced: the reference VM in fast-forward, SETUP
answered at frames 3000 and 3300, and the picture on screen sampled once per
emulated second from frame 3300; `compare_intro.py OUT SHOTS` matches each
86Box picture in order to the nearest of this machine's pictures (the
`shots/` of a `video_compare.py` run), in 6-bit DAC values (86Box expands the
DAC as floor(v*255/63), this machine as v<<2|v>>4). On `vmt386dos500` with
the 1989 drive preset: 86 pictures compared, 85 exact and one close (p037, a
sample taken mid-way through the title screen's fade-in, between two of our
consecutive frames). Every picture that is not exact must be listed, with its
hash and the reason, in `expected_misses86.txt`, or `pc_parity.py` fails. Two
runs give the same pictures at the same times; about 48 s.

`--vnc` keeps the older real-time capture over VNC on the 286 profile
`D:\86box\vmf` (below). Its result (6 Oct 2026): 105 distinct
pictures, 104 within 3 levels per channel of ours in order, none exact,
because a 1 Hz sample on a 6 MHz 286 lands on a different frame of the same
scene.

## The VNC profile (`D:\86box\vmf`) and running by hand

`86box.cfg.vnc` here is its configuration: `ami286`, 640 KB,
`hdc_1 = xtide_at`, VGA, AdLib, `vid_renderer = vnc`, `sound_muted = 1`, no
floppy image (if one is configured the BIOS boots it first). The disk image
comes from `build_hdd.py` then `patch_hd_boot.py` (FreeDOS plus the GOG files
in `C:\F117A`); `nvr/ami286.nvr` from `cmos_ami286.py`; `roms` is a junction
to the release's ROM folder.

    powershell -File tools\ref86box\start_vnc86.ps1 -Profile D:\86box\vmf
    py tools\ref86box\vnc86.py "wait 55; type f117; key Return; wait 10; shot a.png"

`start_vnc86.ps1` uses Qt's offscreen platform, below-normal priority and
OpenAL's null device. While no client is connected the machine is paused.
`vnc86.py` words: `wait`, `shot`, `type`, `key`, `click`. The first run needs
about 55 s of real time to reach the DOS prompt. Stop with
`Stop-Process -Name 86Box`.

**What did not work** (so nobody retries it): PostMessage key events to the
release build's window, with or without focus, including from a separate
Windows desktop (`hidden86.ps1`: the window captures invisibly but cannot be
typed into). Floppy-first boot order, a CMOS extended-memory size the VM does
not have, a zero BPB geometry and an MBR without boot code each stopped the
boot before DOS.

## Silent speech waveform reference

Apply `86box-audio.patch` in addition to `86box-trace.patch`, then rebuild.
`B86_AUDIO=PREFIX` records the emulation-thread mixer buffers before host
gain/playback. It creates `PREFIX.sound.pcm` (48,000 Hz) and
`PREFIX.music.pcm` (49,716 Hz): interleaved stereo, signed little-endian int32
containers **in signed-16-bit amplitude units**. A raw s32le decoder needs a
65536 gain to recover those units; otherwise a converted int16 file is silent.
The hook never changes a buffer, timer or guest state, and flushes each block
so a deliberately stopped run keeps all completed blocks. Keep the runner's
`offscreen`, OpenAL `null` and SDL `dummy` settings; nothing plays aloud.

Before using a capture, run identical input with `B86_AUDIO` unset and set,
with `B86_SPKLOG=OUT/speaker.log` for both: on 9 Oct (`sound86.py OUT
--frames 4500`) all 4,500 frame records, 1,621 AdLib writes and the
speaker-port log were byte-identical; the two 64-second guest runs took about
28 s together.

For the speech capture, copy `tools/routes/cargo_pilot.input` privately and
change SETUP's second answer from scancode 03/83 (2) to 02/82 (1), the IBM
speaker. Run `b86_cargo_pilot.py --data GAME --front PRIVATE_INPUT --out OUT
--seconds 30` with `B86_AUDIO=OUT/reference` and
`B86_SPKLOG=OUT/speaker.log` on the MS-DOS 5.00 VM. It captures the takeoff
call (94 s of wall time); the short flight's supply-drop verdict is expected
to fail because no delivery was attempted, and it is not mission-completion
evidence.

Run our interpreter with that private input, `--steps 4000000000`, its recorded
`--time-us`, `--speaker-log` and `--opl-log`, using a private save directory
(about 50 s). Render those logs with `audio_render OPL_LOG OURS_WAV 9000000
4000000000 --speaker-log SPEAKER_LOG --speaker-model realsound` (1.6 s). Then
compare:

```powershell
py tools/ref86box/speech_audio.py `
  --reference-pcm OUT/reference.sound.pcm --reference-ports OUT/speaker.log `
  --ours-wav OURS_WAV --ours-ports SPEAKER_LOG --out COMPARED `
  --capture-off OFF_DIR --capture-on ON_DIR
```

The tool selects the longest continuous count-write run, strips leading 128
silence and requires identical counts before comparing audio. It extracts
250 ms of pre/post-roll, resamples both to 44,100 Hz mono, compares two bands
and excludes only the call's first/last 20 ms switching transient. It reports
signed waveform correlation, envelope correlation, alignment and RMS level;
no passing PCM-equality or listening verdict is inferred from those metrics.
The output includes two clipped WAVs, input SHA-256 hashes and `report.json`.
A changed count stream is rejected; repeated reports are byte-identical.

Measured 9 Oct 2026 (`D:/f117-gate/p1-speech/compared/report.json`): 12,924
non-silence counts identical, 855.625500 ms on 86Box and 855.625556 ms here.

| Band | Waveform correlation | Envelope correlation | Lag | Ours/reference RMS |
|---|---:|---:|---:|---:|
| 300-3400 Hz | -0.991565 | 0.999516 | 0 ms | 3.16856 |
| 100-8000 Hz | -0.988890 | 0.999437 | 0 ms | 3.15896 |

The sign and level are explained by the independent models. 86Box's
`snd_speaker.c:speaker_update` maps count to `(count/256)*10240 - 5120`;
our mode-0 PIT stays low for count clocks, so its average over the measured
79-clock carrier falls as count rises, with a 10,000-unit swing
(`src/host/audio.c`, `speaker.c`). The expected magnitude ratio is
`10000*256/(79*10240) = 3.16456`, between the two band measurements.
The residual reflects sampling, carrier averaging and the DC blocker.
This is the independent speech waveform reference for Phase 1;
physical-speaker listening remains Phase 4.
