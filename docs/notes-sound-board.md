Design note (not yet implemented): the sound listening board, and from it hybrid sound.

**Purpose.** Hear every sound the game can make through each of the three drivers side by side
(AdLib, Roland MT-32 through Munt, PC speaker), mark each one as music, sound effect or speech, and
mark which driver sounds best. The marks decide hybrid sound's split (MT-32 music with AdLib effects)
and its defaults. Owner's request, 24 Oct 2026.

**Inventory of sounds.** Every driver has the same four entry points: 0 init, 1 raise an id,
2 resync the DOS clock, 3 the sequencer tick (VGAME's timer ISR calls it six ticks in seven,
51.43 Hz), plus 4 a noise step (memory note f117a-sound-map). ASOUND.117's id table holds 46 near
pointers at cs:0911. Speech is id 0x28 with a segment (a buffer of samples), so it is its own row.
Every id the game actually raises is confirmed by a census of the original's calls to the driver
entry (trap 1E42:0313 / linear 0x1E733 for VGAME; memory note f117a-call-site-census), not assumed
from the table: an id only reachable as a multi-request sequence is rendered as that sequence.

**Rendering.** A headless harness, one run per (driver, id): boot to a quiet point with that driver
chosen, call entry 1 with the id through the machine (guest call, as the matched-routine harness
does), run the timer ISR's cadence until the driver's voices fall silent or a cap, and log the
output with `f117run`'s existing `--opl-log`, `--midi-log` and `--speaker-log`. Render the logs with
`tools/audio_render.c` (the application's own audio path; Munt for MIDI with the owner's ROMs).
Trim leading and trailing silence; encode compactly. The game logic is untouched: this only calls
the driver's own entry points.

**The page.** A private claude.ai artifact: one row per sound, three players, a kind selector
(music / effect / speech), a preferred-driver choice and a note. Marks persist in the artifact's
shared database so they survive sessions and can be read back to build hybrid sound. Audio goes
in the artifact's asset store (private by default).

**Hybrid sound itself (after the marks).** The game keeps the Roland driver (its music, logic and
timing are Roland mode's); a hidden AdLib driver on a machine copy receives the same requests (the
same trap), and only the ids marked "effect" are heard from it. Speech defaults to AdLib's path
(switchable). Muting the MT-32's own effects: drop the MIDI channels the Roland driver uses for
effect ids, identified from the board's renders.

**Order of work.** 1) id census from real routes; 2) the render harness; 3) render all three drivers;
4) the page; 5) owner marks; 6) hybrid sound. Steps 2-3 are CPU work: not during a Phase 2 gate.

**Progress on step 1 (24 Oct, read-only, light work during the bd gate).** VGAME's call sites to the
sound gate `vgame_sound_request` (0xD3F9) push (priority, id) as the last two words before the call;
most are literal in the translated C (`cpu_push16(c, N)`), found by grepping `src/matched/matched.c`
for the `NEAR_THEN`/`VG2_NEAR`/`VG3_NEAR`/`TNEAR`/`GNEAR`/`WNEAR`/`RPNEAR`/`guest_call` macros that
call it and reading the two pushes immediately before. First pass, 19 call sites, found literal ids
0, 4, 5, 6, 8, 9, 10, 11, 14, 15, 17 (0x11) and 60 (0x3C); comments name two of them ("destruction
sound" id 5, "gun sound" id 6). Corrected the extraction (only read pushes before the call macro's own
token on its line, not a following statement folded onto the same source line - two sites first misread
a later statement's pushes as the id): confirmed against their own comments, e.g. "Crash: the sound,
a pause of 60 ticks, then the flight's end (5)" at 0x2B6D pushes (priority 2, id 4), and the twin crash
check at 0x429B pushes the same (2, 4). Clean ids from 19 sites, in (priority, id) push order (id is
always the last push, read as the callee's first argument): id 4 crash, 5 destruction, 6 gun, 8, 9, 10,
11, 12 (idle throttle, dynamic priority from the current throttle value), 14, 15, 17 (0x11) - eleven
distinct ids. This is a first pass, not the census: still needed are (a) each driver's own table (ASOUND.117's 46 entries; RSOUND's and ISOUND's own
counts, which need not number the same ids the same way), and (b) cross-checking against a real
route's trapped driver-entry calls (not just static reads), since a static push does not prove the
branch runs.

**A second gate found.** `0xD414` is only ever called from inside `vgame_sound_request` itself (the
0xD3F9 gate's own driver call), so the 19 call sites above are the complete set reachable through that
gate. But `vgame_engine_sound` (matched at linear 10743, VGAME's continuous per-frame engine note) is
a second, separate path: it pushes a literal id straight to a different far entry, `0xD43A` (not yet
matched/translated), with no priority word. Its id is 0x19 (25) normally, or 0x18 (24) when idle,
parked and below a speed threshold - this is the idle/throttle engine note the sound map calls "the
engine note's definition" at DS 0x0E94. Running total of distinct VGAME-raised ids: 4, 5, 6, 8, 9, 10,
11, 12, 14, 15, 17, 24, 25 (thirteen). `0xD43A`'s own body (what it does with the id, and whether it
has further gates of its own) is still unread.

**Confirmed: one driver entry point.** Drafted both `0xD414` and `0xD43A` straight from VGAME.EXE's
image (`tools/matched_draft.py`): both are a bare `lcall 0x1e42, 0x313` and nothing else (pop the
argument word, ret). So every VGAME sound request, from either gate, converges on the single far call
`1E42:0313`, matching the old project's memory note (`f117a-sound-requests.md`) independently from this
codebase's own image. `1E42` is a runtime load segment (wherever SETUP loaded the resident driver that
session), not an address in VGAME.EXE's own image, so the real per-id dispatch has to be read from each
driver file's own image at its own entry offset (ASOUND.117's documented table: 46 near pointers at
cs:0x0911) - next step, one driver at a time.
