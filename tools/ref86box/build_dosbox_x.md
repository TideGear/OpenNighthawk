# DOSBox-X as a headless parity check

DOSBox-X can run with no window and no sound (`-silent`) and type into the
guest by itself (`AUTOTYPE`), but its video capture only starts from a host
hotkey, and posted key messages do not reach it (it ignores input while the
window is not focused, even on a hidden desktop). `dosbox-x-auto-video.patch`
fixes both: it starts AVI capture at initialisation when `DBX_AUTO_VIDEO` is
set (the capture begins on the first rendered frame every time), and it
schedules scripted input in emulated time: `DBX_AUTO_INPUT="ms|k|a;ms|c|x,y;..."`
(keys, mouse moves and clicks in 320x200 guest pixels) starts when DOS first
executes the program named in `DBX_AUTO_INPUT_AT`, so no host input is
involved (`tools/save_parity.py` builds the string from a route file;
`DBX_AUTO_LOG=FILE` logs each event). The pointer is set directly
(`Mouse_AutoPosition`): DOSBox-X's own absolute move goes through the host
pointer and is ignored in a window.

## Build (about 15 minutes after the downloads; MSYS2 as in build_86box.md)

1. `pacman -S autoconf automake libtool make nasm` and the UCRT64 freetype,
   libpng, zlib, SDL2_net, libslirp, fluidsynth and ffmpeg packages.
2. `git clone --depth 1 https://github.com/joncampbell123/dosbox-x.git`
   (this was master at 2026.10.01), then `git apply
   dosbox-x-auto-video.patch`.
3. In an MSYS2 UCRT64 shell in the checkout: `./build-mingw` (it builds its
   own SDL1; the result is a static `src/dosbox-x.exe`, no DLLs needed).
   After a source change only `make -j10` in `src/` is needed, run from
   `bash -lc` with `MSYSTEM=UCRT64` and `TEMP` pointing at a writable folder
   (plain `make` from another shell fails with "Cannot create temporary file
   in C:\Windows").

## Use

`py tools/video_compare.py --data GOG_DIR --dosbox PATH\dosbox-x.exe
--seconds 130 --work-dir DIR` runs the intro on DOSBox-X headless (the GOG
configuration under a layer that forces windowed mode and silence, SETUP
answered by AUTOTYPE) and compares its pictures with this machine's, as for
GOG's DOSBox. DOSBox-X records mode 13h at 640x400, each pixel doubled; the
tool reads it back to 320x200 and reports the few frames where a palette
write landed between the two scanlines of a pair.

`py tools/save_parity.py --data GOG_DIR --no-86box` plays a route's START
inputs here and on this machine and compares the saved `ROSTER.FIL` byte for
byte (identical, 6 Oct 2026). The music is judged from the capture's audio by
`tools/sound_parity.py`: envelope 0.90-0.92, spectral 0.947; DOSBox-X plays it
at 0.74 of the level GOG's DOSBox and this machine produce (its mixer), which
is reported, not judged.

## Closed loop (DBX_LOOP_*)

`DBX_LOOP_STATE=FILE` and `DBX_LOOP_REPLY=FILE` start a tick every
`DBX_LOOP_EVERY` emulated milliseconds (default 200) once `DBX_AUTO_INPUT_AT`
names the running program. Each tick appends `seq time max left cpu` and the
bytes of `DBX_LOOP_READS` (`off:len,...`, DS-relative, DS = PSP + 0x10 + 0x1e42
as in `tools/cargo_check.py` `observe`) to the state file. The emulator then
waits in real time, with emulated time stopped, for a reply line
`R <seq> <spec>` in the reply file; `<spec>` uses the `DBX_AUTO_INPUT` format,
relative to that tick. `stop` ends the loop. If no reply comes within 60 s the
loop ends with a message on stderr.

Checked on SETUP (7 Oct 2026): ticks fall 200 ms apart; a reply of
`300|k|n;400|k|n` at tick 3 (600.8 ms) taps n at 920.8 and 1020.8 ms; two runs
gave identical state and event lines. `max=9000` is cycles per millisecond,
the same 9 MIPS as `tools/machine_api.py`. Execs after the loop ends differ by
about 1 ms between runs, because emulated time then runs freely.

**Never run a GUI build over GOG's `dosboxF117A.conf` without `fullscreen=false`
in a layer after it; that file asks for fullscreen.**

## First result (6 Oct 2026, intro, 130 s)

1,237 exact RGB pictures in order over 124.2 s; unmatched inside the
alignment: DOSBox-X 26, here 95 (two and one of them lasting more than one
sample); timing drift -71 to +200 ms, +200 ms at the end. GOG's DOSBox
0.74 on the same comparison matches 1,329 pictures with three unmatched on
each side and no end drift, so DOSBox-X is the looser reference here: its
timing differs from the model by about a fifth of a second over the intro.
