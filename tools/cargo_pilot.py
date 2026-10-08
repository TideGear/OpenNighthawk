#!/usr/bin/env python3
"""Closed-loop supply drop: fly the generated mission and release the crate by the aircraft's own state.

The front end (SETUP, START's mission choice and the hangar) replays the recorded keys and clicks of
a route until VGAME starts; the flight then runs on this pilot's controls (tools/recon_pilot.py's
navigation, the cargo bay selected with Space and the crate released with Enter once the target is
inside the release window). The verdict is cargo_check.py's own: one timely impact in the original's
delivery area, with no D5 credit unless --fix D5 is on.

    py tools/cargo_pilot.py --data GOG_DIR --front tools/routes/cargo.input --out DIR \
        [--release-lo 60 --release-hi 300] [--fix D5]
"""
import argparse
import csv
import json
from pathlib import Path
import re
import sys
import tempfile

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

from machine_api import Machine  # noqa: E402
from recon_pilot import control as navigate  # noqa: E402
from cargo_check import observe, errors, impacts  # noqa: E402
from strike_pilot import strike_state  # noqa: E402

CARGO_WEAPON = 18
MIN_DIVE_AGL = 140
last_station_press = [-99]
last_release_press = [-99]


cargo_slot = [None]


def pilot_state(machine):
    # Once found, the crate's slot is followed after it lands (its TTL reads 0 then), as cargo_check does.
    state = dict(strike_state(machine))
    seen = observe(machine, cargo_slot[0])
    if seen["cargo_slot"] >= 0:
        cargo_slot[0] = seen["cargo_slot"]
    state.update({k: v for k, v in seen.items() if k != "stations"})
    return state


LEVEL_PITCH = 211
PULL_UP_PITCH = 2000
# 200 survived the Machine's and DOSBox-X's own tracks, but on 86Box, whose track differs run to run,
# three of six flights ended in the original's draw-detected terrain collision (exit 129) at 150-290.
HOLD_AGL = 400
RUN_IN_RANGE = 835
RUN_IN_AGL = 400
# Measured on the approach (about 590 knots): AGL holds at pitch 0-100, the pitch stays where a key
# hold leaves it, and AGL changes by about (pitch - 50) / 40 per tick.
RUN_IN_LEVEL = 50
# The crate starts at AGL - 20 with the aircraft's pitch ([0x2DF0]); a crate whose height lands on
# exactly 1 gets pitch 0 there and expires (bugs.md D5, approach trap), so its chance of being trapped
# is about one over its fall per frame. A pitch above 0 is pulled down each frame until it crosses 0,
# and where it ends up depends on the frame rate: about -224 on a 9 MIPS PC (S = [0x368E] 7-8), -43
# on 86Box's 386DX/33 (S = 4), where it then falls one unit a tick and is all but certain to trap. A
# pitch at or below 0 is kept, so the release is a dive: the crate keeps the aircraft's -1200 or less
# and falls several units a frame on any machine. A steep crate carries little forward (about 0.3
# map units per unit of height, against 1.4 at -224), so the window is 60-300 short of the target.
RELEASE_DIVE = -1600
RELEASE_PITCH = -1200
RELEASE_AGL_MIN = 150


def in_run_in(state):
    return not state["launch_events"] and state["target_range"] <= RUN_IN_RANGE


def dive_pitch(state, release_lo, release_hi):
    # The approach holds HOLD_AGL with a gentle altitude feedback (the navigator's own glide levelled
    # the aircraft far out and below 200 AGL the pitch swings by several hundred units around any
    # command). The crate takes the aircraft's pitch at the release and falls at about 1/43 of it per
    # tick, and a crate that only reaches height 1 expires instead of impacting; a release near level
    # still falls once the pull-up below has turned the nose. The ground near the target is above the
    # release height, so the release is followed at once by a pull-up (a flight that did not pull up
    # ended in the original's terrain collision, exit 129).
    if state["launch_events"]:
        return PULL_UP_PITCH if state["agl"] < 700 else LEVEL_PITCH
    if state["agl"] < MIN_DIVE_AGL:
        return 500
    if in_run_in(state) and release_lo <= state["target_range"] <= release_hi:
        return RELEASE_DIVE
    if in_run_in(state):
        # The hold below presses only for errors over 100, so it leaves AGL anywhere from about 150
        # to 250 and porpoises; the run-in holds RUN_IN_AGL within a few units for the release.
        return int(max(-400, min(400, RUN_IN_LEVEL + 8 * (RUN_IN_AGL - state["agl"]))))
    if state["agl"] > HOLD_AGL + 40:
        return -650
    return int(max(-600, min(500, 190 + 2 * (HOLD_AGL - state["agl"]))))


def control(machine, state, tick, release_lo, release_hi):
    # The navigator flies the bank and the heading and never fires. It treats weapon 16 as "already
    # selected" (otherwise it presses Space for its own station, fighting the cargo selection below),
    # and photos=1 keeps its photo release off. Its throttle-back near the target is hidden (throttle 0
    # above 240 knots) so the crate leaves at the baseline's speed. Its glide (which levelled the
    # aircraft at 190 AGL far from the target) is replaced by dive_pitch. The cargo bay is selected here.
    navigate(machine, dict(state, weapon=16, photos=1,
                           throttle=0 if state["speed"] >= 240 else state["throttle"]), tick,
             select_key="b", select_range=6000, aim_range=6000,
             camera_aim=False, pitch_tolerance=10 ** 6)
    cargo = [i for i, s in enumerate(state["stations"])
             if s["weapon"] == CARGO_WEAPON and s["stores"] > 0]
    release = (bool(cargo) and state["station"] == cargo[0] and state["launch_events"] == 0
               and release_lo <= state["target_range"] <= release_hi
               and state["agl"] >= RELEASE_AGL_MIN and state["pitch"] <= RELEASE_PITCH
               and tick > last_release_press[0])
    # Every tick, not every third: on 86Box the dive's chance can last two ticks and a press can be
    # missed; once the one crate is gone its store count reads 0 and no further press is sent.
    pitch_error = dive_pitch(state, release_lo, release_hi) - state["pitch"]
    # The game ignores Enter while a pitch arrow is held, so a releasing tick sends no pitch press.
    if abs(pitch_error) > (30 if in_run_in(state) else 100) and not release:
        # One millisecond of hold moves the pitch about 3.5 units; a press is capped at one tick.
        machine.type(machine.clock + 1, r"\D" if pitch_error > 0 else r"\U",
                     hold_ms=max(10, min(190, int(abs(pitch_error) / 4.5))))
    at = machine.clock + machine.ips * 18 // 100
    if not cargo:
        return
    if state["station"] != cargo[0]:
        # One Space per three seconds: a press lands in the station readout only after a few ticks.
        if tick - last_station_press[0] >= 15:
            machine.type(at, r"\s", hold_ms=20)
            last_station_press[0] = tick
    elif release:
        machine.type(machine.clock + machine.ips * 2 // 100, r"\r", hold_ms=20)
        last_release_press[0] = tick


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--data", required=True)
    parser.add_argument("--front", type=Path, required=True,
                        help="a recorded input file whose front end (before VGAME) is replayed")
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--release-lo", type=int, default=60)
    parser.add_argument("--release-hi", type=int, default=300)
    parser.add_argument("--seconds", type=int, default=1500)
    parser.add_argument("--engine", choices=("recomp", "interp"), default="recomp")
    parser.add_argument("--fix", action="append", default=[], help="a switchable fix (docs/bugs.md), e.g. D5")
    args = parser.parse_args()
    lines = args.front.read_text().splitlines()
    header = re.fullmatch(r"# f117r-input ips=9000000 time_us=(\d+)", lines[0] if lines else "")
    if not header:
        raise ValueError("the front file needs its recorded start clock header")
    for line in lines[1:3]:
        if line.startswith("# f117r-fixes "):
            args.fix = sorted(set(args.fix) | set(line.split()[2].split(",")))
    replay = [line.split() for line in lines[1:] if line and not line.startswith("#")]
    args.out.mkdir(parents=True, exist_ok=False)
    rows, position, tick, start = [], 0, 0, None
    with Machine(args.data, tempfile.mkdtemp(dir=args.out), log=args.out / "run.log", engine=args.engine,
                 time_us=int(header[1]), fixes=tuple(args.fix)) as machine:
        machine.record(args.out / "input.log")
        while machine.clock < 5_000_000_000 + args.seconds * machine.ips:
            if machine.program != "VGAME.EXE":
                if start is not None:
                    break                       # the flight is over; later recorded inputs are not this run's
                while position < len(replay) and int(replay[position][1]) < machine.clock + machine.ips:
                    p = replay[position]
                    if p[0] == "K": machine.key(int(p[1]), int(p[2], 16))
                    else: machine.mouse(int(p[1]), *map(int, p[2:5]))
                    position += 1
                if machine.run_until(machine.clock + 90_000) != Machine.SLICE:
                    break
                continue
            if start is None:
                start = machine.start
                print("flight", machine.clock, flush=True)
                # Keep the first-minute controls of the strike pilot: brakes off, throttle up, nose up.
                state = pilot_state(machine)
                if state["flags"] & 8: machine.type(start + 80_000_000, "0")
                machine.type(start + 100_000_000, "+")
                machine.type(start + 170_000_000, r"\D", hold_ms=1000)
            elapsed = machine.clock - start
            if elapsed > 190_000_000:
                state = pilot_state(machine)
                rows.append(dict(clock=machine.clock, seconds=elapsed / machine.ips, **state))
                if tick % 50 == 0:
                    print({k: state[k] for k in ("target_range", "altitude", "speed", "throttle", "weapon",
                                                  "store_count", "launch_events", "cargo_ttl")}, flush=True)
                if impacts(rows) and machine.clock - impacts(rows)[0][1]["clock"] >= machine.ips:
                    break
                if elapsed > args.seconds * machine.ips:
                    break
                control(machine, state, tick, args.release_lo, args.release_hi)
                tick += 1
                step = machine.ips // 5
            else:
                step = 90_000
            if machine.run_until(machine.clock + step) != Machine.SLICE:
                break
        machine.screen(args.out / "final.ppm")
        failures = errors(rows) if rows else ["no observed flight"]
        report = dict(clock=machine.clock, hash=f"{machine.hash:016x}", program=machine.program,
                      errors=failures, fixes=args.fix, release=[args.release_lo, args.release_hi],
                      observation=rows[-1] if rows else None)
        (args.out / "result.json").write_text(json.dumps(report, indent=2) + "\n")
        with (args.out / "flight.csv").open("w", newline="") as stream:
            if rows:
                writer = csv.DictWriter(stream, fieldnames=rows[0]); writer.writeheader(); writer.writerows(rows)
        print(json.dumps(report), flush=True)
    return int(bool(report["errors"]))


if __name__ == "__main__":
    raise SystemExit(main())
