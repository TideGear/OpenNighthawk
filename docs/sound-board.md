# Sound board and hybrid sound (design)

Status: design, not implemented. Step 1 (the id census) is in progress; the Macintosh recordings
are decoded. Owner's request, 24 October 2026.

## Purpose

A listening page that plays every sound the game can make through each DOS driver side by side
(AdLib, Roland MT-32 through Munt, PC speaker), with a fourth column for the Macintosh release's
own digitised recording of the same sound. The owner marks each sound as music, effect or speech
and picks the driver that sounds best. The marks decide hybrid sound's split (MT-32 music with
AdLib effects, a Phase 3 re-release feature in [roadmap.md](roadmap.md)) and its defaults.

## Established

**The driver interface.** From the Reimp's verified map of `ASOUND.117` (see
[provenance.md](provenance.md)): entry 0 initialises, 1 raises a sound id, 2 resyncs the DOS
clock, 3 is the sequencer tick and 4 a noise step. The driver hooks no interrupt; VGAME's timer
ISR (`0x1CB4`) calls entry 3 on six ticks in seven (the counter `[0x264B]` skips the seventh) and
entry 4 every tick. ASOUND's id table holds 46 near pointers at `cs:0x0911`. Speech is id 0x28,
raised with the segment of the loaded `SPEECH.117` samples, so it needs a row of its own.

**One driver entry for all of VGAME.** Every VGAME sound request ends in the far call
`lcall 1E42:0313`. `1E42` is a relocated segment in the image's numbering (`0x33A8:0313` in a run
loaded at `0x1566`), the address the Reimp traps as the driver entry (load-relative `0x1E733`).
Two far routines make the call, each nothing but that `lcall` and a return (drafted from
VGAME.EXE's image with `tools/matched_draft.py`); their callers push the id and pop it after:

- `0xD414`, reached only from the priority gate `vgame_sound_request` (VGAME `0xD3F9`). Callers
  push (priority, id), the id last, so the gate reads the id as its first argument. The gate
  admits a request whose priority is at least `[0x43F2]` (signed); while `[0xC09A]` is set, only
  priorities above 1 pass. Admitted ids go to `0xD414`.
- `0xD43A`, reached from `vgame_engine_sound` (VGAME `0xD41F`), which pushes a literal id with
  no priority: 0x18 when `[0x43F2]` is 0, `[0x2E0A]` is nonzero and `[0xC09A]` is 0, otherwise
  0x19. The gate calls it after every request, admitted or refused; it is also called directly
  (the matched calls return to `0x2106`, `0x3A75` and `0xCB24`).

The per-id dispatch therefore lives in each driver file, read at its own entry offset one driver
at a time.

**Ids VGAME raises, first static pass.** Reading the two pushes before each call to `0xD3F9` in
`src/matched/matched.c` (the call macros `NEAR_THEN`, `VG2_NEAR`, `VG3_NEAR`, `TNEAR`, `GNEAR`,
`WNEAR`, `RPNEAR` and `guest_call`) gave 19 call sites and eleven distinct ids: 4 (crash),
5 (destruction), 6 (gun), 8, 9, 10, 11, 12 (idle throttle, with a priority computed from the
throttle), 14, 15 and 17 (0x11). Both crash checks, at `0x2B6D` and its twin at `0x429B`, push
(priority 2, id 4); the first one's comment reads "Crash: the sound, a pause of 60 ticks, then the
flight's end (5)". Only pushes before the macro's own token count: a later statement folded onto
the same source line first misread as ids 0 and 60. With 0x18 and 0x19 that is thirteen ids.
Outside the gate, VGAME sends 0x17 every frame with the airspeed (the engine note,
[bugs.md](bugs.md) D36) and 0x28 for speech.

## Open questions

- **The full census.** The first pass saw matched code only, and a static push does not prove its
  branch runs. `src/matched/matched.c` now holds at least 27 distinct return addresses for calls
  to `0xD3F9`, so 19 is not the whole set. The denominator is a scan of VGAME's image for every
  call to `0xD3F9` and `0xD41F`, each then reached from a real route with the driver entry trapped,
  comparing ordered lists, not counts.
- **Each driver's own table.** ASOUND's 46 entries are mapped; RSOUND's and ISOUND's tables need
  not number the same ids the same way.
- **The Mac music's DOS ids** (see below).
- **Which MIDI channels the Roland driver uses for effect ids**, needed to mute them in hybrid
  sound; to be read from the board's renders.

## The Macintosh column

The Mac port kept the DOS effect ids and added 300 (DOS `0x04`-`0x14` are Mac resources 304-320),
so effects get a Mac player in the same row. All 33 Mac `snd ` resources are decoded to WAV in a
private folder; the three music pieces still need their DOS ids, and the Mac speech is nine
recorded words the game chains into phrases, against DOS's single 3.3 s `SPEECH.117`, so speech
gets rows of its own. Detail and sources: [other-versions.md](other-versions.md). The Amiga offers
no column: its five 8SVX samples are front-end and intro sounds, and its flight engine holds none.

## Design

**Rendering.** A headless harness, one run per (driver, id): boot to a quiet point with that
driver chosen, call entry 1 with the id through the machine (a guest call, as the matched-routine
harness does), run the timer ISR's cadence until the driver's voices fall silent or a cap, and log
the output with `f117run`'s `--opl-log`, `--midi-log` and `--speaker-log`. Render the logs with
`tools/audio_render.c` (the application's own audio path; Munt for MIDI with the owner's ROMs),
trim leading and trailing silence and encode compactly. Only the driver's own entry points are
called; game logic is untouched. An id only reachable as a multi-request sequence is rendered as
that sequence.

**The page.** A private claude.ai artifact: one row per sound, the driver players and the Mac
player, a kind selector (music, effect, speech), a preferred-driver choice and a note. Marks
persist in the artifact's shared database, so they survive sessions and can be read back to build
hybrid sound. Audio, the Mac recordings included, goes only into the artifact's private asset
store, never the repository.

**Hybrid sound.** The game keeps the Roland driver (its music, logic and timing are Roland
mode's). A hidden AdLib driver on a machine copy receives the same requests (the same trap), and
only the ids marked "effect" are heard from it. Speech defaults to AdLib's path (switchable). The
MT-32's own effects are muted by dropping the MIDI channels the Roland driver uses for effect ids.

## Order of work

1. The id census from the image and real routes (in progress; first static pass above).
2. The render harness.
3. Render all three drivers. Steps 2 and 3 are CPU work: not during a Phase 2 gate.
4. The page (the Mac recordings are ready for it).
5. The owner's marks.
6. Hybrid sound.

## Findings (29 October 2026)

- **`SPEECH.117` is the nine Mac words in one file.** 26,408 bytes of headerless unsigned 8-bit audio (centre
  about 125) at 8,000 Hz: 3.30 s. Heard by the owner as "primary, target, hit, good, nice, landing, cleared, for
  take off, to land" - the same nine words as the Mac recordings 1000-1008, in the same order. The word boundaries
  were cut at the quiet gaps, each chosen as the gap nearest to where the Mac word lengths predict it (all eight
  within 0.04 s): at 0.52, 0.88, 1.10, 1.34, 1.59, 1.95, 2.30 and 2.76 s (the owner listened to the nine clips and
  confirmed the cuts, 29 October). This also gives the subtitle text
  (roadmap "Accessibility": subtitles for the speech).
- **Where the MT-32 ROMs are:** `C:\Users\Tideg\f117-recomp-local\roms` (21 files).
- **The render harness works (29 October 2026, AdLib).** `f117run --steps N --sound-sweep ID,ID,...
  [--sound-span CLOCKS] [--sound-out PREFIX]` (`src/host/headless.c`): with VGAME running at the end of `--steps`,
  each id is rendered on its own copy of the machine; the game's own request stub (the far jump slot at
  load+`1E42:0313`) is made a bare return in the copy so the game's engine notes stay silent, the driver's entry
  (the stub's jump target; `0505:0B98` for ASOUND) is called with the id as the game pushes it, and the copy runs on
  for the span with the OPL, MIDI and speaker writes logged to `PREFIX<id>.opl|.midi|.spk`, plus `PREFIXsweep.tsv`.
  First run on `boot_to_flight` (AdLib), `--steps 2450000000`: id 6 (gun) 70 OPL writes, id 4 (crash) 1,129;
  rebased to the request and rendered with `audio_render`, both give real audio (the gun about 0.65 s; the crash
  still sounding at the 2 s span, so long sounds need a longer one). Not yet: the id list, Roland and speaker
  routes, music from START and END, an automatic span, and the page.
- **The page exists (29 October 2026).** `tools/sound_board_page.py` builds one self-contained HTML file from the
  rendered sweeps (`tools/sound_board_render.py`), the Mac and Amiga WAVs and the nine speech words, with
  `tools/sound_board_template.html`: 158 clips, 38 DOS sound rows (AdLib, Roland through Munt with the 1.07
  ROMs, PC speaker, and the Mac recording where the Mac port's id offset of 300 gives one), the nine speech
  words from DOS and Mac, and the Mac-only and Amiga sounds. Marks (kind, best source, note) are saved through
  the page's `db`. The audio is generated and never committed. Sweeps used: `boot_to_flight` with the SETUP
  answer 2 (AdLib), 3 (Roland) and 1 (PC speaker), `--steps 2450000000`, ids 0-45, a 5 s span. Not done: music
  from START and END (their own programs), longer spans for the sounds that reach the 5 s limit, ids 41-44
  (the driver call did not return within its limit, though output was logged).

## Hybrid sound in the app (30 October 2026)

`--mac-sounds DIR` (or `mac-sounds =` in the ini) loads Mac recordings named `<resource>.wav`. When the game makes its
far call into the sound driver (VGAME `1E42:0313`, the id at SS:SP+4), a host execution hook (`machine_hooks.exec`)
plays the recording for ids that have one (`macsound.c`: effects 4 to 20 as Mac resource 300 + id; music 36, 37, 38 as
resources 322, 321, 200) and makes the call return at once, so the DOS driver never sees that request. `--music-source`
and `--sound-source` (`default`, `mac`, `dos`) override the default order (`soundpolicy.c`).

### Why a machine-layer hook, and what it costs

The first design put the hook in the matched sound gate (`matched.c`, VGAME `0D3F9`). A full seeded flight showed that no
VGAME matched routine runs in a build without generated code, so that hook could not be tested outside the multi-hour
generated-code build and the P2 gate. The hook moved to the machine loop instead (`pc.c`, beside the existing `trap_on`
check). Tradeoffs:

| | machine-layer hook (chosen) | hook in the matched gate |
|---|---|---|
| Works in every build and both engines | yes (to be confirmed for generated-code builds, below) | only where generated code runs the matched routine |
| Test turnaround | minutes | hours (generated-code build, then the P2 gate) |
| Touches `matched.c` (P2 gate territory) | no | yes |
| Touches the core machine loop | yes: one extra comparison per instruction when off, which must change nothing | no |
| Sees the game's priority | no: only the id; the original gate has already admitted the request | yes |
| Parity of recordings and replays | identical with `--mac-sounds` off (the check is skipped); with it on, the driver sees fewer requests, so a recording made that way is not bit-for-bit comparable with the original | same |

Known limits:
- **Generated code may bypass the loop.** In a build with generated code, a translated block that reaches the driver
  stub from inside generated code may never return to the machine loop, so the hook could miss it. Unverified; the test
  so far is with the interpreter-based build. If a generated-code build misses requests, the hook belongs at the
  driver's entry in the generated code as well.
- **The driver's own state.** A swallowed request leaves the driver untouched, as if the game had not asked. The game
  keeps asking every frame for the engine note (ids 23 to 25), which have no recording and still go through.
- **One DOS driver at a time.** The loaded DOS driver is the one chosen at SETUP. The rule's Roland music and AdLib
  effects together need a second driver fed the same requests; this change does not do that. Sounds with no Mac
  recording are played by whichever driver is loaded.
- **START and END.** They call their own thunks (`0A95:6BB1`, `05FA:25F3`), not the VGAME stub, so their sounds are not
  taken yet.
- **Speech.** The Mac has the nine words, not the DOS phrases, so speech stays with the DOS driver.
