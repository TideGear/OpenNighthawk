# Historical reports reviewed, 12-13 October 2026

These reports are closed for Phase 3 without claiming that every historical
machine was unaffected. Their evidence and reopening criteria remain in
[bugs.md](bugs.md). No speculative game patch was added.

| Report | Original source | Result and limit |
|---|---|---|
| D10, mountain collision | MicroProse Macintosh 2.3.0 release notes, 22 July 1994, in the 2.3.1 update archive | The notes establish a Mac defect. No DOS equivalent is established. The repeated static stack walk flags only five known runtime paths among 490 VGAME census entries; it cannot exclude stray writes. |
| D35, fast-PC startup | [DOS Days troubleshooting](https://www.dosdays.co.uk/topics/Games/game_f117.php) | All 18 fresh probes reach START: three speeds, two engines, three sound choices. Intros are skipped with normal Enter inputs. This does not reproduce full movie playback on a real fast PC. |
| D36, missing Roland engine | [DOS Days troubleshooting](https://www.dosdays.co.uk/topics/Games/game_f117.php) | Available MT-32 control ROMs all produce engine audio in Munt; their 128 PCM-table entries agree. Physical units and MT-100/CM-32L PCM ROMs remain untested. |

## Startup and stack checks

Run each startup case in a fresh process, after building the machine API:

```powershell
py tools/startup_probe.py --data GAME_DIR --ips 1000000000 --sound 2 --engine interp --skip-intro
py tools/stack_balance.py --gen GENERATED_DIR --census CENSUS_TSV
```

The reviewed startup matrix uses 200,000,000, 1,000,000,000 and
5,000,000,000 instructions per second; `interp` and `recomp`; sound 1
(speaker), 2 (AdLib), and 3 (Roland). It records the program transitions,
clock and final hash, and requires arrival at START. The check applies
normal inputs only and leaves the installed game files untouched.

The stack walk's five reports are E6BE, E876, F338, F7AE and F8F0:
startup/exit paths, stack probing, and a file-write error path. These are
the same intentional SP movements previously investigated. The check
stops tracking after frame restoration and does not follow indirect jumps;
it is evidence against a straightforward imbalance, not a proof against
all memory corruption.

## Roland check

The earlier normal landing MIDI capture establishes that the game uploads
"Jet Engine", selects program 7 on channel 8 and starts note 23 when moving.
The new staged check keeps that captured initialization and first active
note, caps long idle gaps at one second, then holds the note for 20 seconds.
It uses the application's offline renderer, the same PCM ROM and seed 117
for every control ROM. The last ten seconds are measured after startup:

| Control ROMs | RMS, signed 16-bit stereo | Peak | Silent |
|---|---:|---:|---|
| 1.04, 1.05, 1.06, 1.07 | 342.49 | 1,848 | No |
| 2.04, 2.06, 2.07 | 292.40 | 1,516 | No |

```powershell
build/audio_render.exe ENGINE_START_MIDI OUT_WAV 9000000 END_CLOCK --mt32 CONTROL_ROM PCM_ROM --seed 117
```

This fixed-note experiment extends the previous moving-flight renders; its
levels are not interchangeable with the earlier 3.4 dB flight difference.
All 128 four-byte PCM table entries match the 1.07 table in these seven
ROMs (table offsets 3000h for 1.0x, 8100h for 2.0x, as used by Munt).
Munt synthesizes from firmware tables and does not execute all original
firmware. A physical-unit recording or an untested model's incompatible
mapping can therefore reopen D36.

## Evidence retention

The local evidence bundle retains original archive/note hashes, startup
logs and results, the staged MIDI stream, ROM identities, wave-table
comparison and audio measurements. Original game files, ROMs, recordings
and raw logs stay outside git. The reports are closed as not reproduced
or not established for DOS, rather than relabeled as fixed bugs.
