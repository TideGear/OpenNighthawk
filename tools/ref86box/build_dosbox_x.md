# DOSBox-X as a headless parity check

DOSBox-X can run with no window and no sound (`-silent`) and type into the
guest by itself (`AUTOTYPE`), but its video capture only starts from a host
hotkey, and posted key messages do not reach it (it ignores input while the
window is not focused, even on a hidden desktop). `dosbox-x-auto-video.patch`
fixes both and adds the hooks below:

| Variable | Effect |
|---|---|
| `DBX_AUTO_VIDEO` | start AVI capture at initialisation (the capture begins on the first rendered frame every time) |
| `DBX_AUTO_INPUT="ms\|k\|a;ms\|c\|x,y;..."` | keys, mouse moves and clicks (320x200 guest pixels) in emulated time, starting when DOS first executes the program named in `DBX_AUTO_INPUT_AT`; no host input is involved (`tools/save_parity.py` builds the string from a route file). The pointer is set directly (`Mouse_AutoPosition`): DOSBox-X's own absolute move goes through the host pointer and is ignored in a window |
| `DBX_AUTO_LOG=FILE` | log each scheduled event |
| `DBX_LOOP_*` | the closed loop (below) |
| `DBX_SPEAKER_LOG=FILE` | the speaker's port writes (below) |
| `DBX_WALL_US=us` | the guest's date and time pinned to the Machine's (below) |
| `DBX_INT1A_TICK="ip,near_ret,dx"` | START's seed tick staged (below) |

## Build (about 15 minutes after the downloads; MSYS2 as in build_86box.md)

1. `pacman -S autoconf automake libtool make nasm` and the UCRT64 freetype,
   libpng, zlib, SDL2_net, libslirp, fluidsynth and ffmpeg packages.
2. `git clone --depth 1 https://github.com/joncampbell123/dosbox-x.git`
   (master at 2026.10.01 was used), then `git apply
   dosbox-x-auto-video.patch`.
3. In an MSYS2 UCRT64 shell in the checkout: `./build-mingw` (it builds its
   own SDL1; the result is a static `src/dosbox-x.exe`, no DLLs needed).
   After a source change only `make -j10` in `src/` is needed, run from
   `bash -lc` with `MSYSTEM=UCRT64` and `TEMP` pointing at a writable folder
   (plain `make` from another shell fails with "Cannot create temporary file
   in C:\Windows").

`pc_parity.py` looks for the executable at `D:\86box-src\dbx-src\src\dosbox-x.exe`
unless given `--dosbox-x PATH`.

**Never run a GUI build over GOG's `dosboxF117A.conf` without
`fullscreen=false` in a layer after it; that file asks for fullscreen.**

## Use

`py tools/video_compare.py --data GOG_DIR --dosbox PATH\dosbox-x.exe
--seconds 130 --work-dir DIR` runs the intro on DOSBox-X headless (the GOG
configuration under a layer that forces windowed mode and silence, SETUP
answered through `DBX_AUTO_INPUT` 5.0 and 5.8 s after SETUP starts) and
compares its pictures with this machine's, as for GOG's DOSBox. It runs in
fast-forward (`[cpu] turbo=true`): the capture takes 11 s instead of 130 s,
and `-time-limit` counts emulated seconds, so it covers the same span. Two
fast-forward captures are byte-identical; real-time ones differ (AUTOTYPE
waits in host time, and so does the pacing), though the comparison report was
the same for both kinds. DOSBox-X records mode 13h at 640x400, each pixel
doubled; the tool reads it back to 320x200 and reports the few frames where a
palette write landed between the two scanlines of a pair.

Result (intro, 130 s): 1,237 exact RGB pictures in order over 124.2 s; a few
unmatched inside the alignment on each side; timing drift -71 to +200 ms.
GOG's DOSBox 0.74 on the same comparison matches 1,329 pictures with no end
drift, so DOSBox-X is the looser reference: its timing differs from the model
by about a fifth of a second over the intro. `pc_parity.py`'s thresholds for
both are at the top of that file.

`py tools/save_parity.py --data GOG_DIR --no-86box [--no-turbo]` plays a
route's START inputs here and on this machine and compares the saved
`ROSTER.FIL` byte for byte (identical, at DOSBox-X's own speed, 227 s).
Fast-forward is not reliable for it: on a busy machine the scripted clicks
missed, a pilot was not erased (61 bytes off) and START once waited 25
minutes, so `pc_parity.py` runs it with `--no-turbo`. SETUP is still answered
by AUTOTYPE there, since the inputs are anchored at START. The music is
judged from the capture's audio by `tools/sound_parity.py`: envelope
0.90-0.92, spectral 0.947; DOSBox-X plays it at 0.74 of the level GOG's
DOSBox and this machine produce (its mixer), which is reported, not judged.

## Closed loop (DBX_LOOP_*)

`DBX_LOOP_STATE=FILE` names the state file and `DBX_LOOP_REPLY=PREFIX` the
reply prefix. A tick starts every `DBX_LOOP_EVERY` emulated milliseconds
(default 200) once `DBX_LOOP_AT` (default `DBX_AUTO_INPUT_AT`) names the
running program. Each tick appends `seq time max left cpu psp` and the bytes
of `DBX_LOOP_READS` (`off:len,...`, DS-relative, DS = PSP + 0x10 + 0x1e42 as
in `tools/cargo_check.py` `observe`) to the state file. The emulator then
waits in real time, with emulated time stopped, for the file `PREFIX<seq>`;
its first line is the spec, in the `DBX_AUTO_INPUT` format and relative to
that tick. One file per tick keeps the reply from being read half-written if
the writer renames it into place. `stop` ends the loop. Spec kinds: `k` a key
tap, `m`/`c` mouse move (and click) in guest pixels, `r` a raw keyboard byte
(hex), `d` the raw mouse driver position `x,y` (the Machine's driver x is 2
times the pixel x), `b` left button down (1) or up (0).

Checked on SETUP: ticks fall 200 ms apart from 0.9 ms; a reply of
`300|k|n;400|k|n` at tick 3 (400.9 ms) taps n at 720.9 and 820.9 ms, each
make 20 ms after its item and each break 40 ms later. Empty replies keep the
run going. `max=9000` is cycles per millisecond, the same 9 MIPS as
`tools/machine_api.py`. Program starts after the loop ends differ by about
1 ms between runs, because emulated time then runs freely.

`tools/dosbox_cargo_pilot.py` drives a pilot through this loop (`--pilot
cargo`, the default; `strike`; `recon`). It mounts a private copy of the
install (`OUT/game`), never the install itself: START rewrites `ROSTER.FIL`
in a front end that creates a pilot, and the Machine reads the same file. Its
reads are the pilot's set in `tools/routes/` (`cargo_pilot.reads`,
`strike_training.reads`, `career.reads`), regenerated from the
Machine with `--trace-reads`; a read outside that set raises KeyError rather
than guessing. A program executed after VGAME ends the flight. It runs in
fast-forward (`--realtime` to turn it off): 107 s wall instead of 12 min, with
all 1,642 ticks, `result.json` and `flight.csv` identical to a real-time run,
since emulated time is still cycles / 9000 per ms. Result for the supply
drop: one timely impact in the delivery area, no credit, as on the Machine;
the two flights differ from the first observed tick (a closed loop, not
lockstep), and DOSBox-X itself is deterministic (two runs, identical ticks).

## The speaker (DBX_SPEAKER_LOG)

`DBX_SPEAKER_LOG=FILE` logs every write to port 42h, to 43h for counter 2, and
to 61h as `emulated-ms port value` (`src/hardware/iohandler.cpp`).
`-silent` also switches DOSBox-X's PC speaker off (`pcspeaker.cpp` returns at
init), so `tools/speaker_parity.py` runs it without that switch, on an
invisible desktop of its own with the SDL dummy drivers and `[mixer]
nosound=true`: no window and no sound, and the AVI capture holds the speaker.
DOSBox-X ignores a mode 0 count written without a new control word
(`PCSPEAKER_SetCounter_NoNewMode`, "FIXME"), so it plays none of the game's
digitised speech, though the writes are there. Those writes, one per counter 0
interrupt, come 78.0 PIT clocks apart on DOSBox-X for the count of 79 the
driver gives counter 0 in mode 2 (79.0 on the Machine, the data sheet's period).

## Wall clock (DBX_WALL_US)

`DBX_WALL_US=us` pins the guest's date and time to the Machine's wall clock.
At the `DBX_AUTO_INPUT_AT` anchor the emulator records `us` and the emulated
millisecond, so `us + (now - anchor)` is the wall time. INT 21h 2Ah/2Ch and
INT 1Ah 02h/04h return that time (BCD for the RTC calls), and the BIOS tick
count at 0x46C is set from it with the Machine's formula (a file written in
the guest carries the Machine's date). The cargo pilot sets `DBX_WALL_US`
from the flight's `time_us` and its setup clock.

## Staged seed tick (DBX_INT1A_TICK)

START seeds its mission generator (`srand` at 0x96BC) from the BIOS tick count,
read by INT 1Ah 00h at 0x8607 when called from 0x7379, as `requestr.pic`'s
decode ends. The tick count stops during the decode on both emulators, and the
decode starts within a few ms of a tick, so the two can read neighbouring
ticks (the Machine 31579, DOSBox-X 31580) and generate different missions.
`DBX_INT1A_TICK="ip,near_ret,dx"` stages DX for the INT 1Ah 00h call whose
return IP and the caller's near return (the word 6 bytes above the interrupt
frame) match; the cargo pilot passes `0x860B,0x737C,31579`. START then takes
the Machine's 267 generator steps, ends on the same state, and VGAME's
mission bytes agree. The staged call logs `int1a tick N staged M` on stderr.

The Machine's value for any route is the BIOS tick at the first change of
the generator (START's data segment, offset 0xAE8C): 31324 on the
strike-training front end (DOSBox-X reads 31325). `--seed-tick` and
`--front-end-clock` (VGAME's exec clock in the Machine) set both for another
route; a scan of ticks around it, one second of flight each, finds the
mission that matches the Machine's target position and loadout.

The route types SETUP's keys through `DBX_AUTO_INPUT`, so the configuration
has no `autotype`: its n and 2 at 5 s would end MPS_LOGO early and skip the
intro, leaving START 100 s longer to idle before the recorded clicks.

**The strike pilot** (`--pilot strike`) flies the strike-training mission with
`strike_pilot.py`'s controls. In the game's key table `b` drops the lock and
`n` selects the next target, so the pilot presses `n` every second tick until
the primary is the lock (it becomes available about 3,050 map units out) and
releases the bomb at 80 units (`--select-key n --select-every 2
--release-range 80`, the defaults). That gives one primary hit and credit on
the Machine and on DOSBox-X over release ranges 40-100 and select intervals 2
and 3, except one Machine release at 40 that left late.

**A career sortie** (`--pilot recon --debrief`) uses the front end of
`tools/routes/career.front` (Serge, Libya, Cold War: a reconnaissance)
recorded on the Machine (`recon_pilot.py --complete --primary-only --debrief
--landing-throttle-gain 0.6`), `--front-end-clock 3061225637 --seed-tick
31233` and the read set `tools/routes/career.reads`. The primary photo is
credited, the aircraft lands and stops on the home runway, END's screens are
taken with `strike_pilot.END_KEYS` through the loop's replies (the loop keeps
ticking after VGAME), and START writes the sortie into `ROSTER.FIL`. Against
the Machine's own closed-loop sortie the rosters differ only in the mission
score (167 against 168, and the running total), because the flights differ:
staging DOSBox-X's flight-result block into our END (`stage_write16`) makes
our END score 167 too. `career_check.career_errors` passes on both. The
shell's blocks sit at different addresses on each machine (the flight block
at 0x20E0 here, 0x88F0 on DOSBox-X, 0x204F0 on 86Box), so such a capture
takes each machine's own pointer.
