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

## The profile (`D:\86box\vmf`)

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

## The check (first result, 6 Oct 2026)

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
