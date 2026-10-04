# Scripted routes

These are inputs to the original game, scheduled relative to each program's
first EXEC. Run them through `tools/run_route.py` so the shell does not
change keyboard escapes. `tools/build_recomp.py` discovers every `.args`
file here and compares the interpreter and generated code at 50-million-
clock checkpoints and the final state.
Each pipeline replay gets a new `save-*` directory, so coverage or a prior
pipeline's edited pilot cannot change its starting state. Coverage reports
also use fresh run directories. Earlier saved rosters are preserved.

The transfer routes select the existing cleared pilot, request a theatre,
tension and mission category, then enter the briefing, arming and hangar.
They take off, exercise flight/weapon/view controls and quit with Alt+Q, Y.
They exercise mission generation and flight; they do not complete the
objectives or establish successful landing.

| Route | World | Tension | Mission category |
|---|---|---|---|
| `korea_strike` | Korea / KO | Limited War | Strike Missions |
| `vietnam_airair` | Vietnam / VN | Conventional War | Air-to-Air Missions |
| `central_america_ground_training` | Central America / CU | Cold War | Ground Strike Training |
| `north_cape_strike` | North Cape / NC | Conventional War | Strike Missions |
| `central_europe_airair` | Central Europe / CE | Cold War | Air-to-Air Missions |
| `middle_east_strike` | Middle East / ME | Limited War | Strike Missions |
| `persian_gulf_air_training` | Persian Gulf / PG | Limited War | Air Combat Training |
| `kuwait_strike` | Kuwait / KU | Conventional War | Strike Missions |

Libya / LB is the default theatre in `boot_to_flight` and `full_cycle`.
The speaker and Roland routes cover the other sound drivers and program
load addresses. Together the routes select all nine worlds, three tension
levels and four mission categories. Individual objective types within a
category still need dedicated routes.

Transfer coordinates come from START's own menu rectangles at DS:6F6E and
the theatre mapping at DS:0DE2. A transfer requires the pilot's clearance;
clicking a theatre on an uncleared pilot leaves Libya selected. The
Middle East route waits longer for mission-order decoding. Korea's carrier
start releases its initial brakes; the six new routes use runway starts
with brakes already released. Toggling brakes on those starts prevents
takeoff and causes an early crash.

`# expect-world STEM` requires START to open `STEM.WLD` and VGAME to open
`STEM.3DG`. `# expect-exit PROGRAM CODE MIN_CLOCKS` requires that program
to exit with the declared code after the minimum elapsed time. Both route
tools enforce these milestones: equal early crashes cannot pass parity.
Airborne screenshots are reviewed separately when adding a flight route.

`roster_edit` creates CHECK using Backspace and Return, edits one character,
cancels another edit with Escape, deletes a KIA pilot with an empty name,
and returns to the office to save. `--move WHEN:X,Y` hovers over a row
without selecting it. `# expect-save FILE OFFSET HEX` checks the committed
bytes independently of engine equality: the name, erased row and cleared
career counters. The 802-byte roster and a fresh-process reload were checked;
the recorded mouse/key session also replays identically under the interpreter.

`frontend_dialogs` opens and dismisses the shipped KIA and retired pilot
dialogs, then uses the MAINT door before and after taking the briefing.
The first arming page has empty stations; the generated assignment fills
the second. The route assigns AIM-9 to station 1 and AMRAAM to station 3
and returns to the office. The dialogs have different OK positions, and
Enter with the pointer outside their button does not dismiss them.
`# expect-open FILE PROGRAM MIN_COUNT` checks two arming-page loads and
four office-page loads; screenshots verify the dialogs and station changes.

Keep run outputs and saves outside the repository. Use a fresh output
directory for a baseline run, since an existing save can change the pilot
and mission:

```powershell
py tools/run_route.py tools/routes/kuwait_strike.args --data D:/GOG/F-117A --out C:/Users/Tideg/f117-recomp-local/check-kuwait -- --hash-every 50000000 --shots 90000000:C:/Users/Tideg/f117-recomp-local/check-kuwait/shot
```
