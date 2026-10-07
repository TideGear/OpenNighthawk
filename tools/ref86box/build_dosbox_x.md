# DOSBox-X as a headless parity check

DOSBox-X can run with no window and no sound (`-silent`) and type into the
guest by itself (`AUTOTYPE`), but its video capture only starts from a host
hotkey, and posted key messages do not reach it (it ignores input while the
window is not focused, even on a hidden desktop). The one-line patch in
`dosbox-x-auto-video.patch` starts AVI capture at initialisation when the
environment variable `DBX_AUTO_VIDEO` is set, so a run is fully hands-off and
the capture begins on the first rendered frame every time.

## Build (about 15 minutes after the downloads; MSYS2 as in build_86box.md)

1. `pacman -S autoconf automake libtool make nasm` and the UCRT64 freetype,
   libpng, zlib, SDL2_net, libslirp, fluidsynth and ffmpeg packages.
2. `git clone --depth 1 https://github.com/joncampbell123/dosbox-x.git`
   (this was master at 2026.10.01), then `git apply
   dosbox-x-auto-video.patch`.
3. In an MSYS2 UCRT64 shell in the checkout: `./build-mingw` (it builds its
   own SDL1; the result is a static `src/dosbox-x.exe`, no DLLs needed).
   After a source change only `make -j10` in `src/` is needed.

## Use

`py tools/video_compare.py --data GOG_DIR --dosbox PATH\dosbox-x.exe
--seconds 130 --work-dir DIR` runs the intro on DOSBox-X headless (the GOG
configuration under a layer that forces windowed mode and silence, SETUP
answered by AUTOTYPE) and compares its pictures with this machine's, as for
GOG's DOSBox. DOSBox-X records mode 13h at 640x400, each pixel doubled; the
tool reads it back to 320x200 and reports the few frames where a palette
write landed between the two scanlines of a pair.

**Never run a GUI build over GOG's `dosboxF117A.conf` without `fullscreen=false`
in a layer after it; that file asks for fullscreen.**

## First result (6 Oct 2026, intro, 130 s)

1,237 exact RGB pictures in order over 124.2 s; unmatched inside the
alignment: DOSBox-X 26, here 95 (two and one of them lasting more than one
sample); timing drift -71 to +200 ms, +200 ms at the end. GOG's DOSBox
0.74 on the same comparison matches 1,329 pictures with three unmatched on
each side and no end drift, so DOSBox-X is the looser reference here: its
timing differs from the model by about a fifth of a second over the intro.
