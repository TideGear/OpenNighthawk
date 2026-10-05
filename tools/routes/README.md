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
result block. `recon_career` continues into END, earns the tenth-mission tour
ribbon and saves the changed score, sortie count and award. Those saved bytes
are asserted independently of engine hash equality.

`strike` destroys Libya training primary target 3 with the loaded Maverick.
It stops airborne after credit and does not establish a return. The stronger
`strike_pilot.py --replay strike.input` observer requires ground-strike type,
target damage, primary credit, one matching hit event, weapon release events,
consumed stores and an airborne aircraft with fuel and no ejection/crash.
`strike_return` replays a separate normal-input flight through a home-33
landing. Run `py tools/strike_pilot.py --complete --replay
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

Dedicated air-to-air objective routes currently cover types 5–8. Type 5 uses
the Vietnam route and default `--time-us 700000000000000`; type 7 uses
`vietnam_airair_type7.front` with `--time-us 700000003000000`; type 8 uses
`vietnam_airair_type8.front` with `--time-us 700000002000000`. Type 6 uses
`central_europe_airair.front` and the default startup clock. Types 6–8 each
release three station-0 AMRAAMs, kill special unit 0 and earn primary credit.
Type 6 returns to home 72 with `--landing-aim 30`; types 7 and 8 return to
home 33, with type 7 using `--landing-aim 50` for the short runway. Their
recorded inputs replay identically under both engines: type 6 has 619
observations and hash `d06a5ccf075b2e80`; type 7 has 780 and hash
`11c82f0235e24265`; type 8 has 741 and hash `6c3336ef1c24da17`. For a close
objective return, `airair_pilot.py` holds cruise altitude while turning onto
the runway heading, then begins the final descent. The other generated
objective types remain open.

`cargo` reproduces original D5 through a normally released supply crate.
`cargo_check.py --replay cargo.input --steps 7498892689` requires one cargo
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
--steps 7498892689` turns the verdict round and requires that credit at the
impact, never before it.

`secret_airstrip_return` delivers at the intact primary strip, turns and
taxis for a second takeoff, then returns to home. Run `airstrip_check.py
--complete --replay tools/routes/secret_airstrip_return.input --steps
18635798708 --data D:/GOG/F-117A --out PRIVATE_DIR` for the strong observer.
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
