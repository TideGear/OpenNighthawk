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
levels and four mission categories. Many individual objective types within a
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

`recon_return` extends the photo route through secondary credit and a raised
home-runway stop. `recon_pilot.py --complete --replay recon_return.input`
checks both original event types, intact targets and the successful parent
result block. Its route args also acknowledge END's debrief so `run_route.py`
can verify a normal END exit after the VGAME return. The current recording
uses an earlier deck descent target and passes under interpreter and
recompiler. `recon_career` continues through END, earns the tenth-mission tour
ribbon and saves the changed score, sortie count and award. Those saved bytes
are asserted independently of engine hash equality.

`strike` destroys Libya training primary target 3 with the loaded Maverick.
It stops airborne after credit and does not establish a return. The stronger
`strike_pilot.py --replay strike.input` observer requires ground-strike type,
target damage, primary credit, one matching hit event, weapon release events,
consumed stores and an airborne aircraft with fuel and no ejection/crash.
`strike_training` is Central America Ground Strike Training started at a clock
(+5 s) that makes it a ground strike, with the laser-guided bomb released
inside its 80-150 map unit window (`docs/bugs.md` D7/D8); the same observer
(`strike_pilot.py --replay strike_training.input`) takes the recording's own
start clock from its header. `strike_return` replays a separate normal-input
flight through a home-33 landing. Run `py tools/strike_pilot.py --complete --replay
tools/routes/strike_return.input --data D:/GOG/F-117A --out PRIVATE_DIR` (add
`--engine interp` for the second observer). It checks that primary hit and
credit persist through stop/debrief, with fuel, gear, brakes and idle gates
and parent result 0/status 3. Interpreter and recompiler observers agree at
297 checkpoints/final hash `a4f05231eacf9d75`.

`vietnam_airair.front` enters Vietnam / Conventional War / Air-to-Air through
normal menu inputs. `airair_pilot.py` steers to the generated special aircraft,
uses the loaded station-0 AMRAAM, observes its seeker cone and release, then
returns to home 33 after objective credit. It checks the slot-0 kill and
primary event at the credit sample, consumed AMRAAM store, and full landing
result. The adaptive input log replays with `--engine recomp` and
`--engine interp` for a direct flight-state parity check.

Dedicated air-to-air objective routes cover types 5–8: `airair_type5` to
`airair_type8`, each a recorded adaptive sortie replayed in the gate, ending
in the debriefing (VGAME exit 129). Type 5 uses the Vietnam front and the
default `--time-us 700000000000000`; type 7 uses `vietnam_airair_type7.front`
with `--time-us 700000003000000`; type 8 uses `vietnam_airair_type8.front`
with `--time-us 700000002000000`. Type 6 uses `central_europe_airair.front`
and the default startup clock. Type 5 releases one station-0 AMRAAM, types
6–8 three; each kills special unit 0, earns primary credit and lands at home
with result 0/status 3. Recorded 6 Oct under the current timing (the 5 Oct
recordings predate the rational VGA schedule and no longer reproduce), each
replays byte-identically under both engines and matches its adaptive run:
type 5 has 946 observations and hash `96c08003b72f6a8b`; type 6 616 and
`33696aa9f6c6f6a8`; type 7 734 and `2b96c2633ed7b639`; type 8 764 and
`f873304b995b58ed`. Type 6 was flown with `--floor 11000 --landing-aim 100
--nudge 0`: at the default 8000 ft pursuit floor it flew into Central
Europe's terrain (the original's draw-detected collision, `[0xC6B2]` ->
`[0x9F96]` -> `flight_end(2)`), and the close-return centreline nudge of 10,
tuned for home 33, puts home 72's touchdown on its box edge. Type 7 used
`--landing-aim 50`. For a close objective return, `airair_pilot.py` holds
cruise altitude while turning onto the runway heading, then begins the
final descent. Strong observer: `airair_pilot.py --replay
tools/routes/airair_typeN.input --engine interp|recomp`.

`cargo` reproduces original D5 through a normally released supply crate.
`cargo_check.py --replay cargo.input --steps 7353559391` requires one cargo
store and release, the actual player weapon slot, ground penetration and
matching impact coordinates inside the delivery area before its deadline.
It also requires an airborne aircraft with fuel and the retained absence of
primary credit for a full second afterwards. Normal TTL expiry cannot pass.
This route stops airborne and does not establish a home return.

`cargo_return` continues the same delivery home: the aircraft holds 8,000 ft
until 14,000 units from home 51, clear of the hills on the northern coast
that ended a 2,500 ft attempt, then lands on the short runway (aim 50 units
before its centre at 200 knots). It is hit on the way out and back (damage
mask 8Eh, eight hits counted) and still returns. `cargo_check.py --complete
--replay tools/routes/cargo_return.input --steps 20000000000` requires the
original no-credit and consumed store throughout the return, then every
landing gate and the parent block's successful result.

`cargo_d5_fixed` replays `cargo.input` with fix D5 switched on (`--fix D5`):
the same impact now earns the original's own primary credit, flag 4000h and
one 8Bh event. `cargo_check.py --fix D5 --replay tools/routes/cargo.input
--steps 7353559391` turns the verdict round and requires that credit at the
impact, never before it.

`d1_fast_machine` is `boot_to_flight` on a 40 MIPS machine (`--ips
40000000`) with fix D1 switched on, every program-relative input time scaled
by 40/9 so the menus get the same emulated seconds. Its strong observer,
`d1_check.py --data D:/GOG/F-117A`, flies the same inputs at GOG's 9 MIPS
without fixes and at 40 MIPS with D1, and requires the settled S, the
mission clock and the frame rate to agree.

`secret_airstrip_return` delivers at the intact primary strip, turns and
taxis for a second takeoff, then returns to home. Run `airstrip_check.py
--complete --replay tools/routes/secret_airstrip_return.input --steps
17850000000 --data D:/GOG/F-117A --out PRIVATE_DIR` for the strong observer.
The target is damaged after delivery; earned credit remains and that later
damage is reported separately.

`career_check.py --initial-roster ACTUAL_ROSTER --replay PHOTO_RECORD
--steps CLOCK_BUDGET --count N --data D:/GOG/F-117A --out PRIVATE_DIR`
earns successive sorties under both engines. Each starts with the preceding
actual 802-byte save copied unchanged. Strict photo/home gates, saved
score/count/status progression, every checkpoint and all save bytes must
agree before another sortie starts. The replay's startup clock is retained.
Promotion can change the generated assignment and require a new recording;
failed flights stop the batch. Use only actual earned saves for this evidence.
`--ahead N` runs the recompiled legs up to N sorties ahead, each in its own
process, with the interpreter legs following from the same input bytes;
sorties are still accepted in order and only when both legs agree.

Keep run outputs and saves outside the repository. Use a fresh output
directory for a baseline run, since an existing save can change the pilot
and mission:

```powershell
py tools/run_route.py tools/routes/kuwait_strike.args --data D:/GOG/F-117A --out C:/Users/Tideg/f117-recomp-local/check-kuwait -- --hash-every 50000000 --shots 90000000:C:/Users/Tideg/f117-recomp-local/check-kuwait/shot
```
