# The owner's reported issues (live testing, 31 October 2026 onward)

One line per thing the owner reported, in the order raised, with its status and what is known. Update this file as each is
worked; do not drop an item until the owner agrees it is fixed. Status: OPEN (not looked at), INVESTIGATING (cause not
established), CAUSE FOUND (not fixed), FIXED (built, in a test copy, not yet confirmed by the owner), CONFIRMED (the owner
checked), EXPLAINED (not a defect: the reason is written down).

## Rendering (Remastered)

| # | What the owner saw | Status | Known so far |
|---|---|---|---|
| R1 | Hangar doors (the yellow lines) do not look right | FIXED, awaiting the owner | The original's line pixels were kept beside Remastered's own (drawn twice); lines were cut short by the ground's depth. Fixed with the outline replacement and the line layer (`docs/remastered-design.md`, "The hangar doors"). |
| R2 | Red light positions do not match the original; one has a grey bar above it | EXPLAINED + FIXED | The original draws its lines and lights from projection records one logic step old, so they sit about 3 px off Remastered's (correct) place. The grey bar was a light squashed by the depth test: fixed with the bright-line layer. |
| R3 | Two black dots and blue bits appear near the hangars (seen in my test, not the owner's) | FIXED, awaiting the owner | Dark lines now stay behind the door as in the original. |
| R4 | A weird bar glitch across the hangars | INVESTIGATING | Not reproduced. Tools: `F117_REMASTER_PROBE`, `F117_REMASTER_KEPT=2`. The owner's screenshot shows a dark band across both hangars at the door level. |
| R5 | When moving, a second image lags behind the first | FIXED in part, awaiting the owner | Remastered now takes its frame from the rerun copy (the same moment as the picture). On the owner's flight the match to Enhanced's picture is about a third closer. Not proven gone. |
| R6 | In the widescreen extensions, stuff does not draw properly, the sides get glitchy when taking off, and the game lags | INVESTIGATING | Lag cause found (R10). The glitchy sides are not reproduced yet: `wt.sh` replays the owner's takeoff in a 1920 x 1080 window. |
| R7 | Within the normal rendering space some stuff no longer draws, like the jet near the runway | INVESTIGATING | In my own takeoff frames the jet is present in the original, the earlier Remastered and the current one. Not reproduced in the owner's session yet. Suspects: the logic-fill depth hair (it lets Remastered replace pixels it does not really draw), the outline replacement. |
| R8 | Objects wholly outside the game's own view are missing in the widescreen margins | OPEN (design chosen) | The owner chose the safe route: the rerun copy with a wider view (`docs/remastered-design.md`, "Plan: Remastered from the rerun copy"). The collector part is built; the wide view is not. |
| R9 | Longer draw distance | OPEN | Same mechanism as R8. |
| R10 | Cutscenes (the intro, the launch) lag | CAUSE FOUND (render cost), fix in progress | The machine and pictures keep real time headless in all three presentations. Remastered costs about 15 ms a picture at scale 3 and about 36 ms in a wide window against a 16.7 ms budget; the line passes (6 ms), the bands (5 ms) and the polygons (2 ms) dominate. The triangle fill now draws only the pixels inside each triangle (exact: 27.6 M pixels against a brute-force test); lines and bands still to do. Remastered also produces about 30% fewer pictures than Enhanced over the same time. |

## Mouse

| # | What the owner saw | Status | Known so far |
|---|---|---|---|
| M1 | In DOS the game can move the mouse cursor (like a button); moving the mouse then continues from there. Here the in-game cursor jumps to where the Windows cursor is | CAUSE FOUND, not built | The host sends absolute positions; the game's INT 33h set-position moves the driver's cursor but not the (hidden) Windows cursor. Fix: warp the Windows cursor to follow the game's whenever it moves it (app only, no change to `src/machine`). |

## Sound

| # | What the owner heard | Status | Known so far |
|---|---|---|---|
| S1 | Roland intro cutscene music does not sound like the AdLib one, ends early, sounds choppy/laggy | INVESTIGATING | Both drivers are asked for the same id (3) once. The songs have their own data in each driver. Rendered from the drivers: AdLib about 30.8 s, Roland about 31.3 s, similar build-up: the Roland song is not shorter. The choppiness fits the render cost (R10) starving the audio loop; not yet proved. |
| S2 | The Roland "pulling out of the hangar" song is what plays for the intro | CAUSE FOUND (probably) | The launch cutscene asks for id 3 again (sequence in the owner's session: 31, 0, 3, 0), so the intro and the pull-out are the same song by the game's own requests. To confirm with the owner what they expected to hear. |
| S3 | The pull-out song did not stop when the cutscene was skipped (it did in the GOG release) | INVESTIGATING | In the owner's session the skip was a mouse click, followed within 545 clocks by id 0, and the Roland driver sends all-notes-off on every channel; Munt silences within a quarter second (offline and through the app's audio path). Not reproduced. Being tested: a key skip (Esc, Space, Enter) instead of the click. |
| S4 | The recomp's sound effects are much quieter than the GOG release | CAUSE FOUND (partly) | Measured here: cannon and gear are about equal on AdLib and Munt, but crash 0.72, destruction 0.30, yellow warning 0.37 and the red warning 0.04 of the AdLib level on Munt. The AdLib path matches GOG's mixer scale. The Munt path adds the MT-32's output unscaled. Which setting the owner compared is not yet known. |
| S5 | The Mac sounds did not play at the hangar | CAUSE FOUND | The test copy predated the new mappings; the hangar door recording (Mac 324) has no DOS counterpart established. |
| S6 | Match all sounds, music and speech across Roland, AdLib, PC speaker and Mac by actual code; say what cannot be matched | IN PROGRESS | Done: every id 0 to 45 has a handler in all three DOS drivers (the PC speaker's id 39 is a bare return); every DOS call site with its id and routine (`/d/f117-gate/remaster-flight-20261031/doscalls.py`, to be made a tool). Not done: the Mac call sites joined to the Mac recordings, the speech phrases against the Mac words, and the list of what cannot be proved. |

## Resolved or answered

- The opening jingle did not play: fixed by the `f117a.ini` with the MT-32 ROM path (CONFIRMED by the owner).
- The test folder was over 100 MB: the two tooling DLLs were not needed; the folder is about 64 MB.
- Cutscene lag is not in the machine or the picture pipeline (timed at real speed in all three presentations).

## Standing notes from the owner

- Phase 2 runs in a separate session: stay in Phase 3 files, check for a running gate before heavy jobs.
- Do not assume code is solid: verify (tests, isolation, measured results).
- The Remastered renderer is the first priority, then hybrid sound; the owner's own look and listen come last.
- Tell the owner when something cannot be matched; do not guess.
