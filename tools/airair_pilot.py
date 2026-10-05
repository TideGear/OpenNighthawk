#!/usr/bin/env python3
"""Fly a generated air-to-air objective with normal input and observe credit.

The observer reads the special enemy aircraft from VGAME's live unit table.
The controller only steers, selects the original air-target mode and fires a
normally loaded missile. On credit it returns to the mission's home base.
"""
import argparse
import json
import math
from pathlib import Path
import re
import tempfile

from landing_pilot import (landing_errors, control as landing_control,
                           observe, signed, clamp)
from machine_api import Machine, RouteInputs
from run_route import route_args


def airair_state(machine):
    state = observe(machine)
    ds = (machine.psp + 0x10 + 0x1E42) << 4
    state["objective_type"] = machine.read16(ds + 0xE304)
    state["target"] = machine.read16(ds + 0xE306)
    state["mode"] = machine.read16(ds + 0xE32A)
    state["display"] = machine.read16(ds + 0xE008)
    state["view"] = machine.read16(ds + 0xC0A6)
    state["air_lock"] = machine.read16(ds + 0x3D92)
    state["ground_lock"] = machine.read16(ds + 0x3D94)
    state["launch_lock"] = machine.read16(ds + 0xE588)
    state["station"] = machine.read16(ds + 0x3680)
    state["bay_switch"] = int(bool(state["flags"] & 4))
    state["event_count"] = machine.read16(ds + 0x951A)
    state["stations"] = []
    for i in range(4):
        kind = machine.read16(ds + 0x3670 + i * 4)
        stores = machine.read16(ds + 0x3672 + i * 4)
        model = machine.read16(ds + 0x36A6 + kind * 0x1A) if kind < 30 else 0xFFFF
        weapon_class = (machine.read16(ds + 0x3394 + model * 0x12 + 0x0C)
                        if model < 32 else 0xFFFF)
        weapon_range = (machine.read16(ds + 0x3394 + model * 0x12 + 0x08)
                        if model < 32 else 0xFFFF)
        state["stations"].append(dict(kind=kind, stores=stores, model=model,
                                       weapon_class=weapon_class,
                                       weapon_range=weapon_range))

    count = min(machine.read16(ds + 0xDEFC), 32)
    units = []
    for i in range(count):
        u = ds + 0xC16A + i * 0x24
        units.append(dict(index=i, target=machine.read16(u),
                          x=machine.read16(u + 2), y=machine.read16(u + 4),
                          z=machine.read16(u + 6), heading=machine.read16(u + 0x10),
                          pitch=signed(machine.read16(u + 0x12)),
                          aircraft=machine.read16(u + 0x16),
                          flags=machine.read16(u + 0x18),
                          endurance=machine.read16(u + 0x1A)))
    state["air_units"] = units
    if units:
        target = units[0]
        state["special_range"] = math.hypot(signed(target["x"] - state["x"]),
                                            signed(target["y"] - state["y"]))
        state["special_killed"] = int(bool(target["flags"] & 0x20))
    else:
        state["special_range"] = None
        state["special_killed"] = 0

    # Class-7 weapons sweep all engaged, live air units and guide toward the
    # nearest one within 0x1000 of the missile's heading. At release the
    # missile starts on the aircraft's current heading, so this predicts its
    # first seeker choice without relying on the cockpit's separate lock UI.
    seeker_target, seeker_range, seeker_error = None, None, None
    for unit in units:
        if not (unit["flags"] & 2) or unit["endurance"] == 0:
            continue
        dx, dy = signed(unit["x"] - state["x"]), signed(unit["y"] - state["y"])
        distance = max(abs(dx), abs(dy)) + min(abs(dx), abs(dy)) // 2
        bearing = math.atan2(dx, -dy) * 32768 / math.pi
        error = signed(int(bearing) - state["heading"])
        if abs(error) <= 0x1000 and (seeker_range is None or distance < seeker_range):
            seeker_target, seeker_range, seeker_error = unit["index"], distance, error
    state["seeker_target"] = seeker_target
    state["seeker_target_range"] = seeker_range
    state["seeker_heading_error"] = seeker_error
    if units:
        dx = signed(units[0]["x"] - state["x"])
        dy = signed(units[0]["y"] - state["y"])
        desired = math.atan2(dx, -dy) * 32768 / math.pi
        state["special_heading_error"] = signed(int(desired) - state["heading"])
        state["vertical_delta"] = units[0]["z"] - state["agl"]
    else:
        state["special_heading_error"] = None
        state["vertical_delta"] = None

    events = [(machine.read8(ds + 0xBA5A + i * 6),
               machine.read8(ds + 0xBA5B + i * 6))
              for i in range(min(state["event_count"], 255))]
    state["launch_events"] = sum(kind == 4 for kind, _ in events)
    state["primary_air_kill_events"] = sum(kind == 0x83 for kind, _ in events)
    state["objective_credit"] = int(bool(state["flags"] & 0x4000))
    return state


def selected_air(lock):
    if lock == 0xFFFF:
        return -1
    index = lock & 0x7F
    return -1 if index == 0x7F else index


def air_weapon_station(state):
    return next((i for i, station in enumerate(state["stations"])
                 if station["stores"] and station["weapon_class"] == 7), None)


def steer_to_special(machine, state, tick, target_station, last_b_request,
                     last_fire):
    """Pursue slot 0 and release a loaded AAM into its seeker cone."""
    if not state["air_units"] or state["special_killed"]:
        return last_b_request, last_fire
    target = state["air_units"][0]
    dx = signed(target["x"] - state["x"])
    dy = signed(target["y"] - state["y"])
    desired_heading = math.atan2(dx, -dy) * 32768 / math.pi
    heading_error = signed(int(desired_heading) - state["heading"])
    bank = clamp(heading_error * 1.5, -6000, 6000)
    roll_error = bank - state["roll"]

    # The player weapon and AI aircraft both use the live AGL altitude here.
    # Keep a safe floor while the objective aircraft is still climbing.
    desired_altitude = clamp(target["z"] + 1500, 8000, 12000)
    want_pitch = clamp((desired_altitude - state["agl"]) * 2,
                       -1000, 1800) + state["trim"]
    pitch_error = want_pitch - state["pitch"]
    at = machine.clock + 1
    if abs(pitch_error) > 220:
        machine.type(at, r"\D" if pitch_error > 0 else r"\U", hold_ms=50)
    if abs(roll_error) > 350:
        machine.type(at + machine.ips // 10,
                     r"\R" if roll_error > 0 else r"\L", hold_ms=50)

    if tick % 10 == 5:
        command = None
        if state["mode"] != 1:
            command = r"\2"                  # F2: air-target mode
        elif state["view"] != 0:
            command = r"\1"                  # F1: cockpit view
        elif state["display"] != 0x13:
            command = "/"                     # forward display / target view
        elif target_station is not None and state["station"] != target_station:
            command = r"\s"                  # cycle to the AAM station
        elif not state["bay_switch"]:
            command = "8"                     # open the weapons bay
        if command:
            machine.type(at + machine.ips * 17 // 100, command, hold_ms=20)

    # B keeps the cockpit's moving-target display useful for a human pilot;
    # the class-7 missile seeker is independent of this display lock.
    air_lock = selected_air(state["air_lock"])
    if (state["mode"] == 1 and state["view"] == 0 and state["display"] == 0x13
            and target_station is not None and state["station"] == target_station
            and state["bay_switch"] and state["special_range"] < 5000
            and abs(heading_error) < 0x2000 and air_lock != 0
            and machine.clock - last_b_request > machine.ips * 2):
        machine.type(at + machine.ips * 17 // 100, "b", hold_ms=20)
        last_b_request = machine.clock

    station_ready = (target_station is not None and state["station"] == target_station
                     and state["stations"][target_station]["stores"] > 0)
    # The weapon's own guidance pass checks all engaged aircraft in a 0x1000
    # cone. Releasing slot 0 while it is the nearest seeker candidate avoids
    # the unrelated frame_weapon_lock_air display interlock (E588).
    if (station_ready and state["bay_switch"] and state["roll"] < 0x3000
            and state["roll"] > -0x3000 and state["seeker_target"] == 0
            and state["special_range"] <= 2500 and tick % 5 == 0
            and machine.clock - last_fire > machine.ips * 2):
        machine.type(at + machine.ips * 17 // 100, r"\r", hold_ms=20)
        last_fire = machine.clock

    if state["throttle"] < 95 and tick % 10 == 0:
        machine.type(at + machine.ips * 30 // 100, "+", hold_ms=20)
    return last_b_request, last_fire


def objective_errors(rows):
    if not rows:
        return ["no observed flight"]
    last = rows[-1]
    failures = []
    if not 5 <= last["objective_type"] <= 8:
        failures.append("primary objective is not an air-combat objective")
    credit_rows = [row for row in rows if row["objective_credit"]]
    if not credit_rows:
        failures.append("primary air objective credit is missing")
    elif not any(row["special_killed"] and row["primary_air_kill_events"]
                 for row in credit_rows):
        failures.append("special aircraft kill and primary event are missing")
    if not any(row["launch_events"] for row in rows):
        failures.append("no normal weapon release was recorded")
    station = air_weapon_station(rows[0])
    if station is None:
        failures.append("no loaded air-to-air weapon was available")
    elif not any(row["stations"][station]["stores"]
                 < rows[0]["stations"][station]["stores"] for row in rows):
        failures.append("no loaded air-to-air store was consumed")
    if last["ejection"] or last["fuel"] <= 0:
        failures.append("ejection/crash state or no remaining fuel")
    return failures


def read_replay(path):
    lines = Path(path).read_text().splitlines()
    header = re.fullmatch(r"# f117r-input ips=9000000 time_us=(\d+)",
                          lines[0] if lines else "")
    if not header:
        raise ValueError("replay requires a recorded 9 MHz input log")
    events = []
    for line in lines[1:]:
        parts = line.split()
        if not parts or parts[0].startswith("#"):
            continue
        if parts[0] != "K" and not (parts[0] == "M" and parts[5:] == ["0", "0"]):
            raise ValueError("only recorded keys and absolute mouse are supported")
        events.append(parts)
    return int(header[1]), events


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--data", required=True)
    parser.add_argument("--out", required=True)
    parser.add_argument("--front-route", default=str(Path(__file__).parent / "routes" / "vietnam_airair.front"))
    parser.add_argument("--seconds", type=int, default=1800, help="flight-time limit")
    parser.add_argument("--engine", choices=("interp", "recomp"), default="recomp")
    parser.add_argument("--time-us", type=int, default=700_000_000_000_000,
                        help="emulated startup clock used to generate the mission")
    parser.add_argument("--landing-aim", type=int, default=20,
                        help="runway-centre offset used on the final approach")
    parser.add_argument("--replay", help="observe a recorded adaptive input log")
    args = parser.parse_args()
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=False)
    replay = None
    time_us = args.time_us
    if args.replay:
        time_us, replay = read_replay(args.replay)
    inputs = None if replay is not None else RouteInputs(route_args(args.front_route))
    rows, tick, last, flight_start = [], 0, "", None
    last_b_request = -10**30
    last_fire = -10**30
    target_station = None
    takeoff_initialized = False
    approach = False
    close_return = None
    turning_home = False
    flight_block = None
    replay_pos = 0
    with Machine(args.data, tempfile.mkdtemp(prefix="save-", dir=out),
                 log=out / "run.log", engine=args.engine, time_us=time_us) as machine:
        if replay is None:
            machine.record(out / "input.log")
        next_report = 0
        while machine.clock < 5_000_000_000 + args.seconds * machine.ips:
            if inputs:
                inputs.poll(machine)
            else:
                while replay_pos < len(replay) and int(replay[replay_pos][1]) < machine.clock + machine.ips:
                    event = replay[replay_pos]
                    if event[0] == "K":
                        machine.key(int(event[1]), int(event[2], 16))
                    else:
                        machine.mouse(int(event[1]), *map(int, event[2:5]))
                    replay_pos += 1

            if machine.program != last:
                last = machine.program
                print(machine.clock, last, flush=True)
                if last == "VGAME.EXE":
                    flight_start = machine.start
                    flight_block = None
                    takeoff_initialized = False

            step = 90_000
            if last == "VGAME.EXE" and flight_start is not None:
                elapsed = machine.clock - flight_start
                if replay is None and not takeoff_initialized and elapsed > 40_000_000:
                    takeoff = airair_state(machine)
                    if takeoff["home"] and takeoff["departure"]:
                        # Match the established strike/recon timing: read the
                        # initialized runway state, then release brakes before
                        # throttle if this generated start has them engaged.
                        if (takeoff["agl"] <= takeoff["ground"]
                                and takeoff["flags"] & 8):
                            machine.type(flight_start + 80_000_000, "0")
                        machine.type(flight_start + 100_000_000, "+")
                        machine.type(flight_start + 170_000_000, r"\D", hold_ms=1500)
                        machine.type(flight_start + 260_000_000, r"\U", hold_ms=500)
                        machine.type(flight_start + 350_000_000, r"\U", hold_ms=300)
                        machine.type(flight_start + 400_000_000, "6")
                        takeoff_initialized = True
                if elapsed > 190_000_000:
                    state = airair_state(machine)
                    flight_block = state["flight_block"]
                    if target_station is None:
                        target_station = air_weapon_station(state)
                        print("mission", {k: state[k] for k in
                              ("objective_type", "target", "air_lock", "stations")}, flush=True)
                    if tick % 5 == 0:
                        rows.append(dict(clock=machine.clock, seconds=elapsed / machine.ips, **state))
                        if elapsed >= next_report:
                            print({k: state[k] for k in
                                   ("special_range", "altitude", "speed", "air_lock",
                                    "launch_lock", "objective_credit", "launch_events",
                                    "primary_air_kill_events", "stations")}, flush=True)
                            next_report = elapsed + 450_000_000
                    if state["objective_credit"]:
                        if not (out / "credit.ppm").exists():
                            machine.screen(out / "credit.ppm")
                        if close_return is None:
                            close_return = state["range"] < 2500
                        waypoint_distance = math.hypot(
                            signed(state["home_x"] - state["x"]),
                            signed(state["home_y"] + 4000 - state["y"]))
                        to_home = math.atan2(
                            signed(state["home_x"] - state["x"]),
                            -signed(state["home_y"] - state["y"])) * 32768 / math.pi
                        if close_return:
                            if waypoint_distance < 1500:
                                turning_home = True
                            south_of_home = signed(state["y"] - state["home_y"]) > 1500
                            aligned_home = abs(signed(int(to_home) - state["heading"])) < 8192
                            if turning_home and south_of_home and aligned_home:
                                approach = True
                        elif waypoint_distance < 150:
                            approach = True
                        if replay is None:
                            landing_state = state
                            if not approach and close_return:
                                # A close return reaches the approach waypoint
                                # before it is pointed toward the runway. Turn
                                # home at cruise altitude before starting descent.
                                landing_state = {
                                    **state,
                                    "home_y": state["home_y"] - 4000 if turning_home
                                    else state["home_y"],
                                    "range": max(state["range"], 2500),
                                }
                            elif (approach and close_return
                                  and state["agl"] > state["ground"]):
                                # Nudge the final path to the narrow runway's
                                # centreline; ground-contact checks still use
                                # the actual home coordinates.
                                landing_state = {
                                    **state,
                                    "home_x": state["home_x"] + 10,
                                }
                            landing_control(machine, landing_state, tick, approach,
                                            cruise=8000, aim=args.landing_aim,
                                            approach_speed=200)
                    elif replay is None and elapsed > 500_000_000:
                        last_b_request, last_fire = steer_to_special(
                            machine, state, tick, target_station, last_b_request, last_fire)
                    if (state["objective_credit"] and state["box"]
                            and state["nearest"] == state["home"]
                            and state["speed"] <= 1 and state["throttle"] == 0):
                        ds = (machine.psp + 0x10 + 0x1E42) << 4
                        previous = state["stopped"]
                        until = machine.clock + step
                        while machine.clock < until and machine.program == "VGAME.EXE":
                            status = machine.run_until(min(until, machine.clock + 128))
                            if status != Machine.SLICE or machine.program != "VGAME.EXE":
                                break
                            stopped = machine.read16(ds + 0x3DC8)
                            if stopped != previous:
                                state = airair_state(machine)
                                rows.append(dict(clock=machine.clock,
                                                 seconds=(machine.clock - flight_start) / machine.ips,
                                                 **state))
                                previous = stopped
                    tick += 1
                    step = machine.ips // 5
                    if elapsed > args.seconds * machine.ips:
                        break
            elif flight_start is not None:
                break

            if machine.run_until(machine.clock + step) != Machine.SLICE:
                break

        machine.screen(out / "final.ppm")
        failures = objective_errors(rows)
        if flight_block:
            report_block = dict(mission_result=machine.read16(flight_block + 0x28),
                                pilot_status=machine.read16(flight_block + 0x26))
            failures.extend(landing_errors(rows, report_block,
                                           (out / "run.log").read_text()))
        else:
            report_block = {}
        report = dict(clock=machine.clock, hash=f"{machine.hash:016x}",
                      program=machine.program, errors=failures,
                      observation=rows[-1] if rows else None, **report_block)
        (out / "result.json").write_text(json.dumps(report, indent=2) + "\n")
        with (out / "flight.jsonl").open("w") as stream:
            for row in rows:
                stream.write(json.dumps(row, separators=(",", ":")) + "\n")
        print(json.dumps(report), flush=True)
    return int(bool(report["errors"]))


if __name__ == "__main__":
    raise SystemExit(main())
