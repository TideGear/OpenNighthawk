#!/usr/bin/env python3
"""Diagnostic landing pilot: observe VGAME, queue normal keys, record input.

Run its recorded input through f117run under both engines for parity checks.
The JSON report distinguishes DOS termination from the mission result stored
in the parent flight block. A DOS exit of 129 alone does not prove a landing.
"""
import argparse
import csv
import math
import json
import re
import tempfile
from pathlib import Path

from machine_api import Machine, RouteInputs
from random_flights import base_route

FIELDS = {
    "heading": 0x2DEE, "pitch": 0x2DF0, "roll": 0x2DF2,
    "agl": 0x2DF4, "altitude": 0x2DF6, "throttle": 0x2E0A,
    "speed": 0xB194, "x": 0xC0D0, "y": 0xC0DE,
    "ground": 0xC0CE, "flags": 0x9B34, "home": 0xE31A,
    "departure": 0xE308, "fuel": 0x3666, "box": 0x3DA2,
    "nearest": 0xE00C, "stopped": 0x3DC8, "S": 0x368E,
    "ejection": 0xC09A, "exit": 0xE57E, "trim": 0x98F6,
    "box_width": 0x9930, "box_length": 0x9B36,
}


def signed(word):
    return (word + 32768) % 65536 - 32768


def clamp(value, low, high):
    return max(low, min(high, value))


def observe(machine):
    # PSP + load paragraph + the original VGAME link-time DGROUP segment.
    ds = (machine.psp + 0x10 + 0x1E42) << 4
    state = {name: machine.read16(ds + offset) for name, offset in FIELDS.items()}
    for name in ("pitch", "roll", "trim"):
        state[name] = signed(state[name])
    target = ds + 0xB2CE + state["home"] * 16
    state["home_x"] = machine.read16(target + 2)
    state["home_y"] = machine.read16(target + 4)
    state["surface"] = 128 if machine.read8(target + 9) & 2 else 0
    state["range"] = math.hypot(signed(state["home_x"] - state["x"]),
                                signed(state["home_y"] - state["y"]))
    state["flight_block"] = (machine.read16(ds + 0xE576) << 4) + machine.read16(ds + 0xE574)
    return state


def landing_errors(rows, report, log, require_dos_exit=True):
    """Reject ground taxis, off-base stops, ejections and early endings."""
    errors = []
    if not rows or not any(r["agl"] > r["ground"] + 100 for r in rows):
        return ["no airborne flight recorded"]
    last = rows[-1]
    def in_box(row):
        return (row["box"] == 1 and row["nearest"] == row["home"]
                and abs(signed(row["x"] - row["home_x"])) <= row["box_width"] >> 5
                and abs(signed(row["y"] - row["home_y"])) <= row["box_length"] >> 5)
    def contact(row):
        return row["agl"] == max(row["ground"], row.get("surface", 0))
    if not any(contact(r) and in_box(r) for r in rows):
        errors.append("no ground contact inside the home approach box")
    if not in_box(last) or not contact(last):
        errors.append("final position is outside the home approach box or above ground")
    if last["speed"] > 1 or last["throttle"]:
        errors.append("aircraft did not stop at idle")
    if last["flags"] & 1 or not last["flags"] & 8:
        errors.append("gear is not down or brakes are not on")
    if last["ejection"] or last["fuel"] <= 0:
        errors.append("ejection/crash state or no remaining fuel")
    normal_exit = bool(re.search(
        r"^\[exit\] VGAME\.EXE terminated with code 129 .* @\d+$", log, re.M))
    successful_handoff = (report.get("mission_result") == 0
                          and report.get("pilot_status") == 3
                          and normal_exit)
    # At the threshold boundary, VGAME can complete the return and hand off
    # before the observer samples the next stopped-counter increment.
    if ((not last["S"] or last["stopped"] <= 16 // last["S"])
            and not successful_handoff):
        errors.append("home completion countdown did not finish")
    if report.get("mission_result") != 0 or report.get("pilot_status") != 3:
        errors.append("parent flight block does not report a successful return")
    if require_dos_exit and not normal_exit:
        errors.append("VGAME did not complete its normal debriefing handoff")
    return errors


def control(machine, state, tick, approach, cruise=2500, aim=20, approach_speed=200,
            deck_aim=220, throttle_gain=.1):
    """Short, separated stick pulses; every key is released normally.

    `cruise` is the altitude held before the approach; a return leg over
    hills can hold higher and lower it near home. `aim` is how far before
    the runway centre the glide path meets the ground, and `approach_speed`
    the speed held on final; a short runway needs both moved."""
    at = machine.clock + 1
    in_box = (state["box"] == 1 and state["nearest"] == state["home"]
              and abs(signed(state["x"] - state["home_x"])) <= state["box_width"] >> 5
              and abs(signed(state["y"] - state["home_y"])) <= state["box_length"] >> 5)
    if in_box and state["agl"] == max(state["ground"], state["surface"]):
        if not state["flags"] & 8:
            machine.type(at, "0")
        elif state["throttle"]:
            machine.type(at, "_")
        return
    dx = signed(state["home_x"] - state["x"])
    dy = signed(state["home_y"] + (0 if approach else 4000) - state["y"])
    # Intercept the runway centreline before descending along it.
    heading = 0 if approach and dy > -150 and abs(dx) < 9 else math.atan2(dx * (4 if approach else 1), -dy) * 32768 / math.pi
    error = signed(int(heading) - state["heading"])
    bank = clamp(error * 1.5, -6000, 6000)
    roll_error = bank - state["roll"]
    altitude = clamp(state["surface"] + (-dy - aim) * 1.6,
                     state["surface"] - 100, 2500) if approach else cruise
    descent = -450 if approach and state["range"] < 1500 else 0
    if state["surface"] and approach:
        # A raised deck has no safe ground before its short approach box.
        # Hold above it until close, then use a gentler descent than the
        # long flat-runway approach.
        altitude = clamp(state["surface"] + (-dy - deck_aim) * 1.6,
                         max(64, state["surface"] - 64), 2500)
        descent = -200 if state["range"] < 1500 else 0
    want_pitch = clamp(descent + (altitude - state["altitude"]) * 2,
                       -1000, 1800) + state["trim"]
    pitch_error = want_pitch - state["pitch"]
    if abs(pitch_error) > 200:
        machine.type(at, r"\D" if pitch_error > 0 else r"\U", hold_ms=60)
    if abs(roll_error) > 300:
        machine.type(at + machine.ips // 10, r"\R" if roll_error > 0 else r"\L", hold_ms=60)
    if tick % 10 == 0:
        command_at = at + machine.ips * 17 // 100
        if state["range"] < 2500:
            want_throttle = clamp(42 + (approach_speed - state["speed"]) * throttle_gain, 0, 85)
            if state["surface"]:
                want_throttle = 85 if state["speed"] < 210 else 50 if state["speed"] > 240 else state["throttle"]
            if state["flags"] & 1:
                machine.type(command_at, "6", hold_ms=20)
            elif abs(state["throttle"] - want_throttle) > 5:
                machine.type(command_at, "-" if state["throttle"] > want_throttle else "=", hold_ms=20)
        elif not state["flags"] & 1:
            machine.type(command_at, "6", hold_ms=20)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--data", required=True)
    parser.add_argument("--out", required=True)
    parser.add_argument("--engine", choices=("interp", "recomp"), default="recomp")
    parser.add_argument("--seconds", type=int, default=1800, help="flight-time limit")
    parser.add_argument("--replay", help="observe a recorded keyboard/mouse flight instead of controlling")
    args = parser.parse_args()
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    route = RouteInputs(base_route() + ["--type", "VGAME.EXE+100000000:+",
                        "--type", r"VGAME.EXE+170000000:~1000:\D"])
    with Machine(args.data, tempfile.mkdtemp(prefix="save-", dir=out),
                 log=out / "run.log", engine=args.engine) as machine, \
            (out / "flight.csv").open("w", newline="") as csvfile:
        machine.record(out / "input.log")
        replay, replay_pos = [], 0
        if args.replay:
            lines = Path(args.replay).read_text().splitlines()
            if not lines or lines[0] != "# f117r-input ips=9000000 time_us=700000000000000":
                raise ValueError("pilot replay requires the route's fixed clock speed and boot time")
            for line in lines[1:]:
                parts = line.split()
                if not parts or parts[0].startswith("#"):
                    continue
                if parts[0] != "K" and not (parts[0] == "M" and parts[5:] == ["0", "0"]):
                    raise ValueError("pilot replay supports recorded keys and absolute mouse input")
                replay.append(parts)
        writer, last, tick, flight_start, approach = None, "", 0, None, False
        rows, screenshots = [], set()
        while machine.clock < 5_000_000_000 + args.seconds * machine.ips:
            if not args.replay:
                route.poll(machine)
            else:
                # Keep only a short future window queued, as f117run does.
                while replay_pos < len(replay) and int(replay[replay_pos][1]) < machine.clock + machine.ips:
                    parts = replay[replay_pos]
                    if parts[0] == "K":
                        machine.key(int(parts[1]), int(parts[2], 16))
                    else:
                        machine.mouse(int(parts[1]), *map(int, parts[2:5]))
                    replay_pos += 1
            program = machine.program
            if program != last:
                print(machine.clock, program, flush=True)
                last = program
            if program == "VGAME.EXE":
                flight_start = machine.start
                elapsed = machine.clock - flight_start
                if elapsed > args.seconds * machine.ips:
                    break
                if elapsed > 190_000_000:
                    state = observe(machine)
                    waypoint_range = math.hypot(signed(state["home_x"] - state["x"]),
                        signed(state["home_y"] + 4000 - state["y"]))
                    if waypoint_range < 150:
                        approach = True
                    row = {"clock": machine.clock, "seconds": elapsed / machine.ips, **state}
                    if writer is None:
                        writer = csv.DictWriter(csvfile, fieldnames=list(row))
                        writer.writeheader()
                    writer.writerow(row)
                    rows.append(row)
                    if state["box"] and state["nearest"] == state["home"] and state["agl"] == state["ground"]:
                        shot = "stopped" if state["speed"] <= 1 else "touchdown"
                        if shot not in screenshots:
                            machine.screen(out / (shot + ".ppm"))
                            screenshots.add(shot)
                    if tick % 50 == 0:
                        csvfile.flush()
                        print({k: round(row[k], 1) for k in ("seconds", "range", "altitude", "speed", "roll", "pitch", "fuel")}, flush=True)
                    if not args.replay:
                        control(machine, state, tick, approach)
                    tick += 1
                    step = machine.ips // 5
                else:
                    step = 90_000
            else:
                if flight_start is not None:
                    break
                step = 90_000
            if machine.run_until(machine.clock + step) != Machine.SLICE:
                break
        machine.screen(out / "final.ppm")
        report = {"clock": machine.clock, "hash": f"{machine.hash:016x}", "program": machine.program}
        if rows:
            block = rows[-1]["flight_block"]
            report.update(mission_result=machine.read16(block + 0x28), pilot_status=machine.read16(block + 0x26))
        print(f"final clock {machine.clock}; hash {machine.hash:016x}; program {machine.program}", flush=True)
    errors = landing_errors(rows, report, (out / "run.log").read_text())
    report["errors"] = errors
    (out / "result.json").write_text(json.dumps(report, indent=2) + "\n")
    for error in errors:
        print("landing failed: " + error, flush=True)
    return int(bool(errors))


if __name__ == "__main__":
    raise SystemExit(main())
