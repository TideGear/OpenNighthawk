# What the sound code proves

Claims about the DOS sound drivers and the host's hybrid sound that were checked against the code, not by ear. What is not proved is said at the end of each section. Tools: `tools/sound_id_proof.py`, `tools/sound_dos_calls.py`, `tools/sound_call_returns.py`.

## Every sound id has a handler in every driver

The game asks a sound driver by a far call to its entry 1 with the id on the stack. In `ASOUND.117` (AdLib), `RSOUND.117` (Roland) and `ISOUND.117` (PC speaker) entry 1 is the same routine: ignore ids above 2Dh, otherwise `call cs:[bx + table]` into a 46-entry table of handlers. `py tools/sound_id_proof.py --data DIR --md docs/sound-id-table.md` reads the table from each file and lists the handler of each id 0 to 45 (`docs/sound-id-table.md`). Proved: every id has its own handler in all three drivers; the PC speaker's id 39 is a bare return. Not proved: that the three drivers' handlers sound alike.

## The driver's answer is read only by the busy-poll

Every DOS call site of the sound thunks (60 of them, `tools/sound_dos_calls.py`) was followed to see whether the caller uses AX after the call (`tools/sound_call_returns.py`). Only the three id 2 polls read it (START `30BF`, END `161A`, PLAYER `01D4`: `or ax, ax; jne` until the driver says 0). So a request the host takes itself (a Mac recording, a sound played by the AdLib driver beside Roland) answers AX = 0 and the game cannot tell; the host never takes id 2, which once froze START.

## The radio calls, ids 41 to 45

In `ISOUND.117` the handlers for ids 41 to 45 (`cs:0757`, `0760`, `0735`, `077B`, `0784`; code segment 019Ch in the file image) each load AX = 1F40h (8,000 a second) and a pointer BX to a list in the code segment, then jump to the player at `cs:0AE6`. The player stores BX, and its timer interrupt (`cs:08E7`) steps through the list in records of three words: the end of a stretch of `SPEECH.117`, its start, and a paragraph offset (0 here); a record whose first word is 0 ends the list. Read from the file:

| id | list at | stretches of SPEECH.117 (samples) | the call |
|---|---|---|---|
| 41 | `78D` | 0 to 9250 | primary target hit |
| 42 | `795` | 4216 to 9250 | target hit |
| 43 | `79D` | 9251 to 10571, 12829 to 15653 | good landing |
| 43 | `7AB` | 10572 to 15653 | nice landing |
| 44 | `7B3` | 15654 to 18342, 18343 to 22118 | cleared for take off |
| 45 | `7C1` | 15654 to 18342, 22119 to 26407 | cleared to land |

The handler for 43 takes the first list when bit 0 of `cs:[2]` is set and the second when it is clear; `cs:[2]` is a counter the driver increments in its timer interrupt (`cs:026C`), so the choice is a coin flip made by timing. The cuts are the edges of the nine words of the Mac release (resources 1000 to 1008, 4048, 6787, 9018, 10440, 12738, 15500, 18110 and 21902 samples in at the Mac's 7,418 a second against the DOS file's 8,000: the two sets of edges differ by 0.5 to 1.5 per cent and the file is 26,408 samples against the Mac's 26,169 in all). The words, in file order: primary, target, hit, good, nice, landing, cleared, for take off, to land. The file has no cut between target and hit, so ids 41 and 42 each cover that pair.

`macsound_speech_words` (`src/host/macsound.c`) carries this table, and with `--mac-sounds` a radio call plays the Mac's words one after another (`tests/test_macsound.c`). For 43 the host uses bit 0 of the DOS clock tick (BDA 0040:006C) in place of the driver's own counter. The Roland (`RSOUND.117`, handlers `0577` `0580` `0589` `05A1` `05AA`, lists at `5B3` `5BB` `5C3`/`5D1` `5D9` `5E7`) and AdLib (`ASOUND.117`, handlers `096D` `0976` `097F` `0998` `09A1`, lists at `9AA` `9B2` `9BA`/`9C8` `9D6` `9E4`) drivers were read the same way: the lists for 41, 42, 44 and 45 are identical to the PC speaker's; for 43 both have the same two calls, "good landing" (9251 to 10571, then 12829 to 15653) and "nice landing" (the AdLib driver writes it as two records, 10572 to 12828 then 12829 to 15653: the same two words), and each chooses by bit 0 of its own tick counter (`cs:[0A69]` in the Roland driver, `cs:[000E]` in the AdLib driver; the AdLib driver's odd and even are the other way round from the PC speaker's). So the three drivers say the same five calls with the same words. Not proved: how the speech sounds through each driver (the PC speaker's pulse widths, the others' sampled playback).

## Stop

Id 0 is the stop request; every driver's handler for it silences its voices. The host now stops the Mac recordings on id 0 too (before, a Mac recording of a skipped cutscene's music kept playing).
