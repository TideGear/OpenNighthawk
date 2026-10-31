# Sound board and hybrid sound

Status (30 October 2026): the sound board page, the render harness and the sound-id census are done. Hybrid sound is
partly built on the branch `p3-hybrid-sound` (the Mac recording player, the source-choice policy and a machine-layer
hook), unmerged and not yet checked in a flight that fires a recorded sound; see "Hybrid sound" below. Owner's
request, 24 October 2026.

## Purpose

A listening page that plays every sound the game can make through each DOS driver side by side (AdLib, Roland MT-32
through Munt, PC speaker), with a column for the Macintosh release's own digitised recording of the same sound. The
owner marks each sound and picks the source that sounds best; the marks decide hybrid sound's defaults (a Phase 3
re-release feature in [roadmap.md](roadmap.md)).

## How the game asks for a sound

From the Reimp's verified map of `ASOUND.117` (see [provenance.md](provenance.md)): the driver's entry 0 initialises, 1
raises a sound id, 2 resyncs the DOS clock, 3 is the sequencer tick and 4 a noise step. The driver hooks no interrupt;
VGAME's timer ISR (`0x1CB4`) calls entry 3 on six ticks in seven and entry 4 every tick. ASOUND's id table holds 46 near
pointers at `cs:0x0911`; RSOUND's and ISOUND's tables need not number the same ids the same way.

**VGAME.** Every request ends in the far call `lcall 1E42:0313` (`0x33A8:0313` in a run loaded at `0x1566`; load-relative
`0x1E733`), the id at SS:SP+4. Two far routines make the call: `0xD414`, reached only from the priority gate
`vgame_sound_request` (VGAME `0xD3F9`; callers push priority then id; a request is admitted when its priority is at least
`[0x43F2]`, signed, and while `[0xC09A]` is set only priorities above 1 pass), and `0xD43A`, reached from
`vgame_engine_sound` (`0xD41F`), which pushes 0x18 when `[0x43F2]` is 0, `[0x2E0A]` is nonzero and `[0xC09A]` is 0, else
0x19. The gate calls it after every request, admitted or refused. Outside the gate VGAME sends 0x17 every frame with the
airspeed (the engine note, [bugs.md](bugs.md) D36) and 0x28 for speech (raised with the segment of the loaded
`SPEECH.117`).

**START, END, PLAYER, MPS_LOGO and DSWAP** call their own thunks (START `0A95:6BB1`, END `05FA:25F3`), so the VGAME stub
does not see their sounds.

## What each sound id is

A census of every far call to each program's sound thunk, found in the unpacked images with the id pushed before it
(`push 0` is "silence", not an id), plus VGAME's gate calls read from the disassembly, each placed in its function from
the matched table.

| id | where it is asked for |
|---|---|
| 2 | palette fades (START, END) |
| 3 | PLAYER.EXE after the logo (intro music) |
| 4 to 20 | flight (VGAME gate): crash 4, destruction 5, gun 6, warnings and gear 7 to 12, and so on; 13 to 16 are weapon releases (a computed id); 19 and 20 from the keys |
| 23 to 25 | engine; 24 and 25 are the two states `0xD41F` re-asks every step |
| 26 | waiting for a button press (START, END) |
| 27 | START world options and pilot options: the highlight moves to a new item |
| 28 | START briefing map marks; END debrief route |
| 29 | START weapons page: one of the four boxes (buttons 19 to 22) is chosen |
| 30 | START weapons page: a weapon is placed in the selected slot |
| 31 | START launch animation (`0448`); VGAME 8 key (bay doors) |
| 32 | VGAME `7663`, release the selected store: refused on the ground or at 80h over a flagged target |
| 33 | START pilot options: a typed character |
| 34 | START weapons page: the choice is refused (the slot already holds it) |
| 35 | cancel (START wait-for-press, END) |
| 36, 37, 38, 39 | END: awards, debrief picture, report picture, landed screen (music) |
| 40 | VGAME exit: release the sound buffer (a driver command, not a sound) |
| 41 to 45 | the five speech phrases (primary target hit, target hit, nice landing, cleared for take off, cleared to land) |
| 21, 22 | no direct caller in any program or the flight gate; a driver sequence command can start sounds 33 to 36 at random, so chained requests are not ruled out |

## The Macintosh column

The Mac port kept the DOS effect ids and added 300 (DOS `0x04` to `0x14` are Mac resources 304 to 320). All 33 Mac `snd `
resources are decoded to WAV in a private folder. Matches by caller (the Mac code asks through `jsr $54a`; the disassembly
is in the private `mac-extract/dis`, not in the repository):

- **Mac 323 (bay door):** the key command at `CODE13:03B2` refuses on its own flag, flips its bay-door word and asks for
  323, the shape of VGAME `C9AE` (8 key: refuse if `[3664]` bit 4, flip `[E58A]`, ask for 31). The two guard flags are not
  proved to be the same thing, nor that the Mac command is the 8 key. The Mac also asks for 323 when one of four items
  (codes 100 to 103) is chosen on its weapons page, where DOS asks for 29.
- **Mac 325 (impact):** a list click on the Mac weapons page (`CODE05:0F52`), where DOS asks for 30.
- **Mac 324 (hangar door):** a door-wipe animation (`CODE12:4D96`) asks for it the first time a four-box screen is shown. No
  code ties it to DOS 31 (the launch animation), and DOS plays nothing on entering its weapons page. Unproven.
- **Mac 500:** the engine loop the player starts for request 311 (spool up) and stops for 312.
- **Music:** Mac 322, 321 and 200 are DOS 36, 37 and 38 (by ear). The Mac asks for its own ids 27 and 28 with no recording
  among those extracted. DOS 34 (refusal) has no Mac counterpart found.
- **Speech:** the Mac has the nine words as separate recordings (1000 to 1008), not the DOS phrases.

The Amiga has five 8SVX samples (front-end and intro sounds) and a 54 KB chip-memory block in its flight program that is
probably more sound data, not decoded. The owner judged the Amiga sounds not worth using. Detail and sources:
[other-versions.md](other-versions.md).

## The render harness and the page

`f117run --steps N --sound-sweep ID,ID,... [--sound-span CLOCKS] [--sound-repeat CLOCKS] [--sound-out PREFIX]`
(`src/host/headless.c`): with VGAME running at the end of `--steps`, each id is rendered on its own copy of the machine.
The game's own request stub is made a bare return in the copy so the engine notes stay silent, the driver's entry (the
stub's jump target; `0505:0B98` for ASOUND) is called with the id as the game pushes it, and the copy runs on for the span
with OPL, MIDI and speaker writes logged to `PREFIX<id>.opl|.midi|.spk`, plus `PREFIXsweep.tsv`. `--sound-repeat`
re-requests the sound every N clocks, as the game does for held sounds (warnings 7 to 9, 11, 12, 14 and the engine); a
single request of those is a blip.

`tools/sound_board_render.py` rebases the logs, renders them with `audio_render` (Munt with the owner's MT-32 1.07 ROMs for
MIDI; ROM folder `C:\Users\Tideg\f117-recomp-local\roms`), trims silence and encodes. `tools/sound_board_page.py` with
`tools/sound_board_template.html` builds one self-contained HTML page (`--board`, `--board-long` for 20 s renders of the
music-length ids, `--board-held` for held versions, `--mac`, `--speech`, `--out`). The page is a private artifact
(https://claude.ai/artifact/F1L8R5uFN1BPhf4aiuCfFK, republished by passing the URL to the Artifact tool): a "Needs your
decision" section on top, everything handled in a collapsed section, a "Plays by default" column that follows the rule
below, and marks (kind, best source, note) saved through `db`, collection `marks`, one document per row key (`dos-<id>`,
`speech-<n>`, `mac-<resource>`). The audio is generated and never committed. Recipe: `boot_to_flight` with the SETUP
answer 2 (AdLib), 3 (Roland) or 1 (speaker) (variants kept outside `tools/routes`, which the gate discovers), `--steps
2450000000`, ids 0 to 45, span 45000000 (5 s at 9 MIPS). The owner's earlier page was lost to an account change and its
marks restored from a backup taken at 13:25 on 30 October; marks made after that are not in the new page.

**Speech.** `SPEECH.117` is the nine Mac words in one file: 26,408 bytes of headerless unsigned 8-bit audio at 8,000 Hz
(3.30 s): primary, target, hit, good, nice, landing, cleared, for take off, to land, cut at 0.52, 0.88, 1.10, 1.34, 1.59,
1.95, 2.30 and 2.76 s (the owner confirmed the cuts by ear). Under the Roland and PC speaker drivers speech is PIT counter
2 mode-0 counts through the PC speaker, so Roland and speaker speech are the same sound, deeper than AdLib's, which plays
speech as OPL writes. The same detector gives the subtitle text.

**Closed:** the AdLib stall horn (14) sounds as it does in the GOG release (owner, 30 October). Its call site (VGAME
`298E`) asks every flight step at priority 1 while the plane sinks or is below 200.

## Hybrid sound

**The owner's rule (30 October 2026, overriding the per-row picks on the page; the player can override it):**

- speech: Mac if available, else PC speaker;
- music: Mac if available, else Roland, else AdLib;
- sounds: Mac if available, else AdLib.

**Built on the branch `p3-hybrid-sound` (unmerged):**

- `src/host/soundpolicy.c` (also on master): chooses a source per kind from what is available, with an override.
- `src/host/macsound.c`: loads `NNN.wav` Mac recordings, resamples, maps game ids to resources (effects 4 to 20 as 300 + id;
  music 36, 37, 38 as 322, 321, 200), mixes them over the DOS driver's output.
- `machine_hooks.exec` (`src/machine`, one comparison beside the existing `trap_on` check in `pc.c`): when execution reaches
  the game's far call into the sound driver, the host may play the recording and make the call return at once, so the DOS
  driver never sees the request.
- App options `--mac-sounds DIR`, `--music-source` and `--sound-source` (default, mac or dos), `f117a.example.ini`.
  Tests: `host_sound_policy`, `host_mac_sound`. The build is clean. In recorded flights the hook sees every request (the engine
  note 23 and 25, speech 44, 40, 0). With the Backspace (cannon) and Enter (fire weapon) keys pressed on the runway it saw
  id 6 four times, each taken for the Mac recording (Mac 306), and id 32 seven times, which confirms that 32 is the refused
  weapon release. Not yet done: listening to the mixed audio, and a flight that fires the other recorded effects.

**Why a machine-layer hook, and what it costs.** The first design hooked the matched sound gate (VGAME `0D3F9` in
`matched.c`). In a full seeded flight (`airair_type5`, VGAME ran for 11.7 billion clocks) the run's own counters showed no VGAME
matched routine dispatched, and the gate's marker never printed. The cause is not established: the file hash of VGAME.EXE
matches the table's (`0x8287450CCA85106F`), so it is not a version mismatch. So that hook could not be tested here, and
the only other route is the multi-hour generated-code build and the P2 gate. Moving it to the machine loop:

| | machine-layer hook (chosen) | hook in the matched gate |
|---|---|---|
| Works where the matched gate does not dispatch | yes, in the build tested; unverified in a generated-code build (below) | unknown: it did not dispatch in the build tested |
| Test turnaround | minutes | hours (generated-code build, then the P2 gate) |
| Touches `matched.c` (P2 gate territory) | no | yes |
| Touches the core machine loop | yes: one comparison per instruction, and it must change nothing when off | no |
| Sees the game's priority | no: only the id (the original gate has already admitted the request) | yes |
| Recordings and replays | identical with `--mac-sounds` off (the hook is never armed); with it on, the driver sees fewer requests, so such a recording is not bit-for-bit comparable with the original | same |

Known limits:

- **Generated code may bypass the loop.** A translated block that reaches the driver stub from inside generated code may
  never return to the machine loop, so the hook could miss it. Unverified.
- **One DOS driver at a time.** The loaded driver is the one chosen at SETUP; Roland music with AdLib effects together
  needs a second driver fed the same requests (a hidden driver on a machine copy receiving the same trap, with the
  MT-32's effect channels muted). Sounds with no Mac recording are played by whichever driver is loaded.
- **START and END** call their own thunks, so their sounds are not taken yet.
- **Speech** stays with the DOS driver: the Mac has the words, not the phrases.
- The machine change (the execution hook table) passed the full gate on 30 October 2026 (`d9fc8b8`; see [p3-handoff.md](p3-handoff.md)).

**Assets.** The Mac WAVs and the MT-32 ROMs are the owner's; the plan for importing them into one library folder is in
[p3-handoff.md](p3-handoff.md). Nothing from the Mac release goes into the repository.
