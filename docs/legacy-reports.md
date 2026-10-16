# Closed historical reports

Three historical reports were reviewed (12-13 October 2026) and closed as
not reproduced or not established for DOS. They are not counted as fixed
bugs, and closing them does not claim that every historical machine was
unaffected. Their reopening criteria are in [bugs.md](bugs.md). No
speculative game patch was added.

| Report | Original source | Result and limit |
|---|---|---|
| D10, mountain collision | MicroProse Macintosh 2.3.0 release notes, 22 July 1994, in the 2.3.1 update archive | The notes establish a Mac defect; no DOS equivalent is established. The static stack walk flags only five known C-runtime paths among 490 VGAME census entries; it cannot exclude stray writes. |
| D35, fast-PC startup | [DOS Days troubleshooting](https://www.dosdays.co.uk/topics/Games/game_f117.php) | All 18 fresh probes reach START: three speeds, two engines, three sound choices. Intros are skipped with normal Enter inputs, so full movie playback on a real fast PC is not reproduced. |
| D36, missing Roland engine | [DOS Days troubleshooting](https://www.dosdays.co.uk/topics/Games/game_f117.php) | All available MT-32 control ROMs produce engine audio in Munt, and their 128 PCM-table entries agree. Physical units and MT-100/CM-32L PCM ROMs remain untested. |

## Startup and stack checks

Run each startup case in a fresh process, after building the machine API:

```powershell
py tools/startup_probe.py --data GAME_DIR --ips 1000000000 --sound 2 --engine interp --skip-intro
py tools/stack_balance.py --gen GENERATED_DIR --census CENSUS_TSV
```

The startup matrix is 200,000,000, 1,000,000,000 and 5,000,000,000
instructions a second; `interp` and `recomp`; sound 1 (speaker), 2 (AdLib)
and 3 (Roland). Each case records the program transitions, clock and final
hash, and must arrive at START. It applies normal inputs only and leaves the
installed game files untouched.

The stack walk's five reports are VGAME `E6BE`, `E876`, `F338`, `F7AE` and
`F8F0`: start-up and exit paths, the stack probe, and a file-write error
path, all of which move SP by design. The walk stops tracking at a frame
restore and does not follow indirect jumps, so it is evidence against a
simple push/pop imbalance, not proof against all memory corruption.

## Roland check

A normal landing flight's MIDI capture shows the game uploading "Jet
Engine", selecting program 7 on channel 8 and starting note 23 once the jet
moves. The staged check keeps that captured initialisation and first engine
note, caps long idle gaps at one second, then holds the note for 20 seconds.
It uses the application's offline renderer with the same PCM ROM and seed
117 for every control ROM, and measures the last ten seconds:

| Control ROMs | RMS, signed 16-bit stereo | Peak | Silent |
|---|---:|---:|---|
| 1.04, 1.05, 1.06, 1.07 | 342.49 | 1,848 | No |
| 2.04, 2.06, 2.07 | 292.40 | 1,516 | No |

```powershell
build/audio_render.exe ENGINE_START_MIDI OUT_WAV 9000000 END_CLOCK --mt32 CONTROL_ROM PCM_ROM --seed 117
```

These fixed-note levels are not comparable with the 3.4 dB difference
measured on the moving landing flight ([bugs.md](bugs.md), D36). All 128
four-byte PCM table entries match the 1.07 table in all seven ROMs (table at
3000h for 1.0x, 8100h for 2.0x, as Munt reads them). Munt synthesises from
the firmware's tables and does not run all of the original firmware, so a
physical-unit recording or an untested model's incompatible mapping can
reopen D36.

## Evidence retention

The private evidence bundle keeps the original archive and note hashes,
startup logs and results, the staged MIDI stream, ROM identities, the
wave-table comparison and the audio measurements. Original game files, ROMs,
recordings and raw logs stay outside git.
