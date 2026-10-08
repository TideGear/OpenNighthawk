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

`DBX_LOOP_STATE=FILE` names the state file and `DBX_LOOP_REPLY=PREFIX` the
reply prefix. A tick starts every `DBX_LOOP_EVERY` emulated milliseconds
(default 200) once `DBX_LOOP_AT` (default `DBX_AUTO_INPUT_AT`) names the running
program. Each tick appends `seq time max left cpu psp` and the bytes of
`DBX_LOOP_READS` (`off:len,...`, DS-relative, DS = PSP + 0x10 + 0x1e42 as in
`tools/cargo_check.py` `observe`) to the state file. The emulator then waits in
real time, with emulated time stopped, for the file `PREFIX<seq>`; its first
line is the spec, in the `DBX_AUTO_INPUT` format and relative to that tick.
One file per tick keeps the reply from being read half-written if the writer
renames it into place. `stop` ends the loop. Spec kinds: `k` a key tap, `m`/`c`
mouse move (and click) in guest pixels, `r` a raw keyboard byte (hex), `d` the
raw mouse driver position `x,y` (the Machine's driver x is 2 times the pixel
x), `b` left button down (1) or up (0).

Checked on SETUP (7 Oct 2026): ticks fall 200 ms apart from 0.9 ms; a reply of
`300|k|n;400|k|n` at tick 3 (400.9 ms) taps n (KBD_n, 35) at 720.9 and 820.9
ms, each make 20 ms after its item and each break 40 ms later. Empty replies
keep the run going. `max=9000` is cycles per millisecond, the same 9 MIPS as
`tools/machine_api.py`. Execs after the loop ends differ by about 1 ms between
runs, because emulated time then runs freely.

`tools/dosbox_cargo_pilot.py` drives the supply-drop pilot through this loop.
Its reads are the set in `tools/routes/cargo_pilot.reads`, regenerated from the
Machine with `--trace-reads`; a read outside that set raises KeyError rather
than guessing.

## Wall clock (DBX_WALL_US)

`DBX_WALL_US=us` pins the guest's date and time to the Machine's wall clock.
At the `DBX_AUTO_INPUT_AT` anchor the emulator records `us` and the emulated
millisecond, so `us + (now - anchor)` is the wall time. INT 21h 2Ah/2Ch and
INT 1Ah 02h/04h return that time (BCD for the RTC calls), and the BIOS tick
count at 0x46C is set from it with the Machine's formula. Checked (7 Oct 2026):
a file written in the guest reads 1992-03-07 20:26, the Machine's date. The
cargo pilot sets `DBX_WALL_US` from the flight's `time_us` and its setup clock.
This does not yet reproduce the Machine's cargo mission: the target matches
(24) from the second tick, but the secondary target is 2 where the Machine has
1, so the flight does not yet match its baseline.

**Never run a GUI build over GOG's `dosboxF117A.conf` without `fullscreen=false`
in a layer after it; that file asks for fullscreen.**

## First result (6 Oct 2026, intro, 130 s)

1,237 exact RGB pictures in order over 124.2 s; unmatched inside the
alignment: DOSBox-X 26, here 95 (two and one of them lasting more than one
sample); timing drift -71 to +200 ms, +200 ms at the end. GOG's DOSBox
0.74 on the same comparison matches 1,329 pictures with three unmatched on
each side and no end drift, so DOSBox-X is the looser reference here: its
timing differs from the model by about a fifth of a second over the intro.
