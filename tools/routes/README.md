# Scripted routes

A route is a file of inputs to the original game: one `f117run` option per
line, `#` starting a comment. Keys and clicks are scheduled relative to a
program's first EXEC (`START.EXE+120000000:258,182`) or replayed from a
recorded `.input` log. Routes hold only what a player would type or click;
everything a run produces stays outside the repository.

Key escapes (`src/host/keys.c`): `\r` Return, `\s` Space, `\e` Escape, `\t`
Tab, `\b` Backspace, `\U` `\D` `\L` `\R` the arrows, `\1`-`\9` F1-F9, `\0`
F10, `\aX` Alt+X, `\cX` Ctrl+X, `\\` a backslash. Run a route with
`tools/run_route.py`, never through a shell: a shell changes these escapes
and so the inputs.
`tools/build_recomp.py` discovers every `.args` file here and compares the
interpreter and the recompiled code at 50-million-clock checkpoints and the
final state. Routes with identical options play one session per engine
(today `recon_return` and `recon_career`), and each is held to its own
milestones on it: 46 routes, 45 sessions. Every replay starts from a fresh
`save-*` directory holding the install's roster, so coverage runs or an
earlier pipeline's edited pilot cannot change its starting state.

## Milestones

Comment lines that `run_route.py` and the gate check after each run, so equal
early crashes cannot pass parity:

| Line | Requires |
|---|---|
| `# expect-world STEM` | START opens `STEM.WLD` and VGAME opens `STEM.3DG` |
| `# expect-exit PROGRAM CODE MIN_CLOCKS` | that program exits with that code, after at least that many clocks |
| `# expect-open FILE PROGRAM MIN_COUNT` | that program opens the file at least that many times |
| `# expect-save FILE OFFSET HEX` | those bytes at that offset of the saved file, independently of engine equality |
| `# seed-roster ROUTE.args` | earn the roster of another route first, in a fresh process, and start from it unchanged (chains are allowed; cycles and failed prerequisites are rejected) |

Milestones prove that a route got where it claims; they do not prove a
landing or a credited objective. Routes marked *observer* below have a
stronger read-only check (next section). Airborne screenshots are reviewed
separately when a flight route is added.

## The routes

**Boot, SETUP and sound drivers**

| Route | What it plays |
|---|---|
| `boot_to_flight` | boot, SETUP (no joystick, AdLib), logo and intro, the pilot form, briefing, arming, a takeoff and a few seconds of flight in the default Libya mission, then Alt+Q, Y |
| `full_cycle` | the whole loop: boot, the front end, a training sortie flown into the ground, the debriefing (END) and back to the front end |
| `ibm_boot`, `roland_boot` | boot into the front end with the PC speaker (SETUP answer 1) or Roland (answer 3) driver; drivers and programs load at other segments than under AdLib |
| `setup_calibration` | full joystick calibration, both buttons, then AdLib (SETUP exits 36) |
| `setup_keypad_original` | the original quits SETUP on keypad 1 with NumLock off (exit 0) |
| `setup_keypad_fixed`, `setup_extended_fixed` | fix D3: keypad 1 is accepted (exit 36); other extended keys still take the original quit path (exit 0) |
| `d1_fast_machine` | `boot_to_flight` on a 40 MIPS machine (`--ips 40000000`) with fix D1, every program-relative input time scaled by 40/9 so the menus get the same emulated seconds; *observer* `d1_check.py` |

**Front end**

| Route | What it plays |
|---|---|
| `roster_edit` | creates pilot CHECK with Backspace and Return, edits one character, cancels another edit with Escape, deletes a KIA pilot with an empty name and returns to the office to save; `--move WHEN:X,Y` hovers over a row without selecting it. Saved-byte milestones check the name, the erased row and the cleared career counters of the 802-byte roster |
| `frontend_dialogs` | opens and dismisses the KIA and retired-pilot dialogs (each has its own OK position; Enter with the pointer outside the button does not dismiss them), uses the MAINT door before and after the briefing, assigns AIM-9 to station 1 and AMRAAM to station 3 (the first arming page has empty stations; the generated assignment fills the second); checks two arming-page and four office-page loads |

**Theatre transfers.** Each selects the existing cleared pilot, requests a
theatre, tension and mission category, takes the briefing, arming and hangar,
takes off, exercises the weapon, target, view and map keys for about a minute
and quits with Alt+Q, Y. They exercise mission generation and flight, not
objectives or landing.

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

Libya / LB is the default theatre of `boot_to_flight` and `full_cycle`, so
the routes select all nine worlds, all three tension levels and all four
mission categories. Many objective types within a category still have no
dedicated route.

**Cockpit keys in the default Libya flight** (each also checks the world and
a minimum exit time, so an early exit fails). These are the real-driver
proofs for matched routines; call counts are in
[roadmap.md](../../docs/roadmap.md).

| Route | What it plays |
|---|---|
| `pause_resume` | Alt+P, Alt+P again (it stays paused), Space resumes, then Alt+Q, Y |
| `hud_layout_toggle` | C twice: the small, then the full HUD |
| `objectives_panel` | F10, C, F10 again in each HUD layout, then quit |
| `navigation_grid` | F3 repeatedly in both HUD layouts, then quit |
| `display_tour` | the multi-function displays and the briefing map, from a log with raw scancodes (`display_tour.input`: the `boot_to_flight` boot and menu inputs as a log, plus the events below): on the START briefing map it hovers and clicks all 15 site markers, Space, and a hover pass; in flight it runs the throttle and brake-less runway roll, NumLock, F7/F8 (navigation display 14h), keypad 3/8/2/4/6/9 (waypoint cursor), F5 (armament 15h), F6, `,` `.` `m` `/` (compass 13h), Shift+`<` (external view), F10 (objectives 18h), F1 (cockpit view), F7 again once airspeed passes 2,000, then Alt+Q, Y. 2.55 G clocks; the grey arrow keys and the keypad with NumLock off do not reach the navigation display |

**Recorded sorties.** Each replays a recorded flight flown with normal keys
and mouse (most by an adaptive pilot tool), so a timing change that moves
the flight invalidates the recording.

| Route | What it plays | Ends |
|---|---|---|
| `strike` | Libya training: destroys primary target 3 with the Maverick | airborne after credit; *observer* |
| `strike_return` | the same credit kept through a normal landing at home 33 and the debrief | landed; *observer* |
| `strike_training` | Central America Ground Strike Training, started at a clock (+5 s) that makes it a ground strike; the laser-guided bomb is released inside its narrow 80-150 map-unit window ([bugs.md](../../docs/bugs.md) D7, D8) | airborne after credit; *observer* |
| `strike_training_d8` | the same mission with fix D8: the bomb released at 1,200 units still hits and earns credit | airborne after credit; *observer* |
| `recon` | Libya: photographs target 1 | airborne after credit; *observer* |
| `recon_darken` | `recon` with F6 at 100 million VGAME clocks (camera darkening) | airborne; no return claim |
| `recon_return` | primary and secondary photos, then a stop on the raised home runway and END's debrief | landed, END exits normally; *observer* |
| `landing` | Libya: flies to home 33, lands and stops | landed (VGAME exit 129); *observer* |
| `cargo` | Persian Gulf supply drop: a timely impact in the delivery area without credit, the original defect D5 | airborne; *observer* |
| `cargo_d5_fixed` | `cargo.input` with fix D5: the same impact earns the original's own credit (flag 4000h, one 8Bh event) | airborne; *observer* |
| `cargo_return` | the same delivery, then home 51: holds 8,000 ft until 14,000 units out (clear of the northern-coast hills), lands on the short runway; hit on the way (damage mask 8Eh) and still returns | landed; *observer* |
| `cargo_pilot` | a closed-loop supply drop: `tools/cargo_pilot.py` released the crate by the aircraft's own state (steep dive, 60-300 units short, 150 ft AGL or more, bay selected) | airborne; *observer* |
| `secret_airstrip` | Persian Gulf type-4 primary by landing at the secret strip | stopped at the strip; *observer* |
| `secret_airstrip_return` | the strip delivery, a ground turn and taxi, a second takeoff and a return to home 58 | landed; *observer* |
| `airair_type5` to `airair_type8` | the four air-to-air objective types: AMRAAMs kill special unit 0 (one missile for type 5, three for 6-8), primary credit, landing at home (72 for type 6, 33 for the others) | landed, debrief (VGAME exit 129); *observer* |
| `camera_gradients` | `airair_type5`'s inputs with Alt+W at 100 million VGAME clocks (gradient camera rendering); the changed drawing cost changes the flight, which ends before the landing | rendering, world loading and a minimum 7B-clock exit only |

**Careers** (saved-roster milestones; every earned save comes from a route,
none is distributed)

| Route | What it plays |
|---|---|
| `career_serge` | selects Serge, completes both photos, returns and saves sortie two |
| `career_promotion` | `# seed-roster career_serge.args`, then a third sortie: promotion and a medal |
| `recon_career` | `recon_return`'s flight continued through END: the tenth-mission tour ribbon, and the changed score, sortie count and award saved |

## Strong observers

Each observer replays the route's recording read-only under either engine
(`--engine interp` or `recomp`) and checks what milestones cannot. All take
`--data INSTALL --out PRIVATE_DIR`.

| Route | Command (inputs under `tools/routes/`) | Checks |
|---|---|---|
| `strike` | `strike_pilot.py --replay strike.input` | ground-strike type, target damage, primary credit, one matching hit event, release events, consumed stores; airborne with fuel, no ejection or crash |
| `strike_training`, `strike_training_d8` | `strike_pilot.py --replay strike_training.input` (or `_d8.input`) | as `strike`; takes the start clock and fixes from the recording's header |
| `strike_return` | `strike_pilot.py --complete --replay strike_return.input` | primary hit and credit persist through stop and debrief; fuel, gear, brakes and idle; parent result 0 / status 3 |
| `recon`, `recon_return` | `recon_pilot.py --replay recon.input`; `--complete --replay recon_return.input` | credited photograph; with `--complete` both event types, intact targets and the successful parent result |
| `landing` | `landing_pilot.py --replay landing.input` | ground contact inside the home approach box, stop at idle, gear and brakes, countdown, successful result |
| `cargo`, `cargo_d5_fixed` | `cargo_check.py [--fix D5] --replay cargo.input --steps 7353559391` | one cargo store and release from the player's weapon slot, ground penetration, matching impact inside the area before the deadline, airborne with fuel; no primary credit for a full second after (with D5: credit at the impact, never before). Normal TTL expiry cannot pass |
| `cargo_return` | `cargo_check.py --complete --replay cargo_return.input --steps 20000000000` | no credit and the consumed store throughout, then every landing gate and the successful parent result |
| `cargo_pilot` | `cargo_check.py --replay cargo_pilot.input --steps 6382359385` | one timely impact in the area, no credit without D5 |
| `secret_airstrip` | `airstrip_check.py --replay secret_airstrip.input --steps 8373782617` | delivery by landing at the strip |
| `secret_airstrip_return` | `airstrip_check.py --complete --replay secret_airstrip_return.input --steps 18635798708` | the return landing; credit earned at delivery is kept, and the strip's later damage is reported separately |
| `airair_type5`-`8` | `airair_pilot.py --replay airair_typeN.input` | the slot-0 kill and primary event at the credit sample, the consumed AMRAAM, the full landing result |
| `d1_fast_machine` | `d1_check.py` | the same inputs at GOG's 9 MIPS without fixes and at 40 MIPS with D1: settled S, mission clock and frame rate agree |

Per-route figures (checkpoints, final hashes, clocks) are in
[parity-audit.md](../../docs/parity-audit.md).

**Career chains beyond the routes.** `career_check.py --initial-roster
ACTUAL_ROSTER --replay PHOTO_RECORD --steps CLOCK_BUDGET --count N --data
INSTALL --out PRIVATE_DIR` earns successive sorties under both engines, each
starting from the preceding actual 802-byte save copied unchanged. Strict
photo/home gates, the saved score/count/status progression, every checkpoint
and all save bytes must agree before the next sortie starts; the replay's
startup clock is kept. Promotion can change the generated assignment and need
a new recording; a failed flight stops the batch. Use only actual earned
saves. `--ahead N` runs the recompiled legs up to N sorties ahead in their
own processes; sorties are still accepted in order and only when both legs
agree.

## Recording notes

- Transfer coordinates come from START's own menu rectangles at DS:6F6E and
  the theatre mapping at DS:0DE2. A transfer needs the pilot's clearance:
  clicking a theatre on an uncleared pilot leaves Libya selected. The Middle
  East route waits longer for its mission orders to decode.
- Korea starts on a carrier and releases its brakes first; the other
  transfers start on runways with the brakes already off, where toggling
  them prevents takeoff and causes an early crash.
- The air-to-air recordings (6 Oct, under the current VGA timing) replay
  byte-identically under both engines and match their adaptive runs. Type 5
  uses `vietnam_airair.front` and the default `--time-us 700000000000000`;
  type 6 `central_europe_airair.front`; type 7 `vietnam_airair_type7.front`
  with `--time-us 700000003000000`; type 8 `vietnam_airair_type8.front` with
  `--time-us 700000002000000`. Type 6 was flown with `--floor 11000
  --landing-aim 100 --nudge 0`: at the default 8,000 ft pursuit floor it flew
  into Central Europe's terrain (the original's draw-detected collision), and
  the default centreline nudge, tuned for home 33, put home 72's touchdown on
  its box edge. Type 7 used `--landing-aim 50`. For a close objective return,
  `airair_pilot.py` holds cruise altitude while turning onto the runway
  heading, then descends.

## Support files

| File | Use |
|---|---|
| `*.input` | recorded input logs that routes replay (`--replay`, relative to the route file) |
| `*.front` | recorded front ends (menu inputs up to VGAME) that the adaptive pilots start from (`--front-route`, or `--front` for the DOSBox-X/86Box pilots): `recon.front`, `career.front`, `career_regular_realistic.front` (`tools/career_chain.py`), `central_america_strike_training.front` (`strike_pilot.py`, with `--time-us 700000005000000`) and the air-to-air fronts |
| `*.reads` | the data-segment read sets the DOSBox-X and 86Box closed-loop pilots answer from (`cargo_pilot.reads`, `strike_training.reads`, `career.reads`), regenerated with `dosbox_cargo_pilot.py --trace-reads` |
| `setup_*.input` | SETUP key logs replayed by the `setup_*` routes |
| `career_rank6.input` | the input `tools/career_rank6.py` replays over a staged rank-6 roster (not a gate route) |

## Running one

Keep run outputs and saves outside the repository, and use a fresh output
directory for a baseline run, since an existing save can change the pilot and
mission:

```powershell
py tools/run_route.py tools/routes/kuwait_strike.args --data "C:\GOG Games\F-117A" --out "$env:USERPROFILE/f117-recomp-local/check-kuwait" -- --hash-every 50000000 --shots "90000000:$env:USERPROFILE/f117-recomp-local/check-kuwait/shot"
```

Options after `--` go to `f117run`; `--engine interp|recomp` chooses the
engine.
