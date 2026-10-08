#!/usr/bin/env python3
"""Diagnostic ground strike through normal controls, with optional return.

Read-only mission/weapon observations select an effective loaded station.
The complete mode also checks home landing and the parent mission result.
"""
import argparse
import csv
import json
from pathlib import Path
import tempfile

from machine_api import Machine, RouteInputs
from random_flights import base_route
from recon_pilot import recon_state, control as navigate
from run_route import route_args
from landing_pilot import control as land, landing_errors, signed
import math
import re


def strike_state(machine):
    state = recon_state(machine)
    ds = (machine.psp + 0x10 + 0x1e42) << 4
    target = ds + 0xb2ce + state["target"] * 16
    target_class = machine.read8(ds + 0xc630 + (machine.read8(target) & 0x7f)) & 15
    stations = []
    for i in range(4):
        weapon = machine.read16(ds + 0x3670 + i * 4)
        stations.append(dict(weapon=weapon, stores=machine.read16(ds + 0x3672 + i * 4),
            effect=machine.read8(ds + 0x3898 + weapon * 16 + target_class),
            weapon_class=machine.read16(ds + 0x36a6 + weapon * 26)))
    state["stations"] = stations
    state["launch_lock"] = machine.read16(ds + 0xe588)
    events = [(machine.read8(ds + 0xba5a + i * 6), machine.read8(ds + 0xba5b + i * 6))
              for i in range(min(state["event_count"], 255))]
    state["launch_events"] = sum(kind == 4 for kind, arg in events)
    state["hit_events"] = sum(kind in (0x81, 0x8c) and arg == state["target"] for kind, arg in events)
    return state


SELECT_EVERY = [0]         # press the select key every this many ticks until designated (0: the navigator, every tenth)
SELECT_KEY = ["b"]        # the key pressed to designate the target: b drops a lock, n takes the next target
RELEASE_RANGE = [0]        # a laser-guided bomb is released at this range from the target (0: the seeker's own interlock)


def control(machine, state, tick):
    # Reuse only its flight/configuration controls; suppress camera selection
    # and photo release. The actual station and strike interlock follow here.
    # The primary strike target is already in the onboard tracking database.
    # Start within camera range and cycle the known contacts with B. N creates
    # a new designation for an unlisted target and can leave the primary until
    # after the aircraft has passed it.
    navigate(machine, dict(state, weapon=16, photos=1), tick,
             select_key=SELECT_KEY[0], select_range=0 if SELECT_EVERY[0] else 6000, aim_range=6000,
             camera_aim=False, pitch_tolerance=100)
    at = machine.clock + machine.ips * 18 // 100
    if SELECT_EVERY[0] and state["target_range"] < 6000 and tick % SELECT_EVERY[0] == 1:
        # Take the next contact until the primary is the lock (n steps one contact a press).
        if not (state["lock"] != 0xFFFF and state["lock"] & 0x7f == state["target"]):
            machine.type(machine.clock + machine.ips * 5 // 100, SELECT_KEY[0], hold_ms=20)
    candidates = [i for i, s in enumerate(state["stations"])
                  if s["stores"] and 0 < s["effect"] < 128 and s["weapon_class"] not in (0, 0xffff, 0xfffe)]
    if not candidates: return
    selected = max(candidates, key=lambda i: state["stations"][i]["effect"])
    if state["station"] != selected:
        if tick % 10 == 5: machine.type(at, r"\s", hold_ms=20)
    elif RELEASE_RANGE[0]:
        designated = state["lock"] != 0xFFFF and state["lock"] & 0x7f == state["target"]
        if designated and not state["launch_events"] and state["target_range"] < RELEASE_RANGE[0] and tick % 10 == 5:
            machine.type(at, r"\r", hold_ms=20)
    elif state["launch_lock"] and state["lock"] & 0x7f == state["target"] and tick % 10 == 5:
        machine.type(at, r"\r", hold_ms=20)


def errors(rows, complete=False):
    if not rows: return ["no observed flight"]
    last = rows[-1]; failures = []
    if last["objective_type"] != 2: failures.append("primary objective is not ground strike")
    if not last["target_damaged"] or not last["flags"] & 0x4000:
        failures.append("primary target damage or objective credit missing")
    if last["hit_events"] != 1 or not last["launch_events"]:
        failures.append("expected primary hit and weapon release events missing")
    airborne = any(s["agl"] > s["ground"] + 100 for s in rows) if complete else last["agl"] > last["ground"]
    if last["ejection"] or last["fuel"] <= 0 or not airborne:
        failures.append("not safely airborne at credit")
    if sum(s["stores"] for s in rows[0]["stations"]) <= sum(s["stores"] for s in last["stations"]):
        failures.append("no loaded store was consumed")
    return failures


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--data", required=True); parser.add_argument("--out", required=True)
    parser.add_argument("--front-route"); parser.add_argument("--seconds", type=int, default=1500)
    parser.add_argument("--replay", help="observe recorded normal keys/mouse without adaptive controls")
    parser.add_argument("--complete", action="store_true", help="also attempt home return after primary credit")
    parser.add_argument("--engine", choices=("recomp", "interp"), default="recomp")
    parser.add_argument("--time-us", type=int, default=700_000_000_000_000,
                        help="the start clock; it seeds the mission generator (a recorded replay needs the default)")
    parser.add_argument("--fix", action="append", default=[], help="a switchable fix to run with (docs/bugs.md), e.g. D8")
    parser.add_argument("--release-range", type=int, default=0,
                        help="release the selected weapon once designated and this close (a laser-guided bomb)")
    parser.add_argument("--select-key", default="b", help="b (the original recipe) or n (next target)")
    parser.add_argument("--select-every", type=int, default=0)
    parser.add_argument("--landing-aim", type=int, default=20,
                        help="runway approach aim relative to centre; negative aims beyond it")
    parser.add_argument("--landing-approach-speed", type=int, default=200)
    args = parser.parse_args(); out = Path(args.out); out.mkdir(parents=True, exist_ok=False)
    RELEASE_RANGE[0] = args.release_range; SELECT_KEY[0] = args.select_key; SELECT_EVERY[0] = args.select_every
    inputs = RouteInputs(route_args(args.front_route) if args.front_route else base_route()) if not args.replay else None
    replay, replay_pos = [], 0
    if args.replay:
        lines = Path(args.replay).read_text().splitlines()
        header = re.match(r"# f117r-input ips=9000000 time_us=(\d+)$", lines[0]) if lines else None
        if not header:
            raise ValueError("replay requires the route's fixed speed and a recorded boot time")
        args.time_us = int(header[1])                  # the recording's own start clock
        for line in lines[1:3]:                        # the fixes it was recorded with
            if line.startswith("# f117r-fixes "):
                args.fix = sorted(set(args.fix) | set(line.split()[2].split(",")))
        for line in lines[1:]:
            parts = line.split()
            if not parts or parts[0].startswith("#"): continue
            if parts[0] != "K" and not (parts[0] == "M" and parts[5:] == ["0", "0"]):
                raise ValueError("only recorded keys and absolute mouse supported")
            replay.append(parts)
    rows = []; tick = 0; initialized = False; flight_start = None; last = ""
    approach = False; flight_block = None; landed_report = {}
    with Machine(args.data, tempfile.mkdtemp(dir=out), log=out / "run.log", engine=args.engine, time_us=args.time_us, fixes=tuple(args.fix)) as machine:
        machine.record(out / "input.log")
        while machine.clock < 5_000_000_000 + args.seconds * machine.ips:
            if inputs: inputs.poll(machine)
            else:
                while replay_pos < len(replay) and int(replay[replay_pos][1]) < machine.clock + machine.ips:
                    parts = replay[replay_pos]
                    if parts[0] == "K": machine.key(int(parts[1]), int(parts[2], 16))
                    else: machine.mouse(int(parts[1]), *map(int, parts[2:5]))
                    replay_pos += 1
            if machine.program != last:
                last = machine.program; print(machine.clock, last, flush=True)
            step = 90000
            if last == "VGAME.EXE":
                flight_start = machine.start; elapsed = machine.clock - flight_start
                if not initialized and elapsed > 40000000:
                    state = strike_state(machine)
                    print("mission", state, flush=True)
                    if not args.replay:
                        if state["flags"] & 8: machine.type(flight_start + 80000000, "0")
                        machine.type(flight_start + 100000000, "+")
                        machine.type(flight_start + 170000000, r"\D", hold_ms=1000)
                    initialized = True
                if elapsed > 190000000:
                    state = strike_state(machine); rows.append(dict(clock=machine.clock, seconds=elapsed / machine.ips, **state))
                    flight_block = state["flight_block"]
                    if tick % 50 == 0:
                        print({k: state[k] for k in ("target_range", "altitude", "speed", "fuel", "weapon", "store_count", "lock", "launch_lock", "launch_events", "hit_events")}, flush=True)
                    if state["flags"] & 0x4000:
                        if not (out / "credit.ppm").exists(): machine.screen(out / "credit.ppm")
                        if not args.complete:
                            machine.run_until(machine.clock + machine.ips)
                            if machine.program == "VGAME.EXE":
                                rows.append(dict(clock=machine.clock, seconds=(machine.clock - flight_start) / machine.ips,
                                                 **strike_state(machine)))
                            break
                        if state["box"] and state["nearest"] == state["home"] and state["agl"] == max(state["ground"], state["surface"]):
                            shot = out / ("stopped.ppm" if state["speed"] <= 1 else "touchdown.ppm")
                            if not shot.exists(): machine.screen(shot)
                    if elapsed > args.seconds * machine.ips: break
                    if not args.replay or (args.complete and replay_pos == len(replay) and machine.clock > int(replay[-1][1])):
                        if args.complete and state["flags"] & 0x4000:
                            waypoint_range = math.hypot(
                                signed(state["home_x"] - state["x"]),
                                signed(state["home_y"] + 4000 - state["y"]))
                            if waypoint_range < 150:
                                approach = True
                            land(machine, state, tick, approach, aim=args.landing_aim,
                                 approach_speed=args.landing_approach_speed)
                        else: control(machine, state, tick)
                    tick += 1; step = machine.ips // 5
            elif flight_start is not None: break
            if args.complete and rows and rows[-1]["box"] and rows[-1]["nearest"] == rows[-1]["home"] and rows[-1]["speed"] <= 1 and rows[-1]["throttle"] == 0:
                # The last countdown increment can be followed by DOS exit
                # inside a normal 200 ms sample. Observe it before the next
                # executable reuses VGAME's memory, without changing state.
                until = machine.clock + step
                ds = (machine.psp + 0x10 + 0x1e42) << 4
                while machine.clock < until and machine.program == "VGAME.EXE":
                    if machine.run_until(min(until, machine.clock + 128)) != Machine.SLICE: break
                    if machine.program == "VGAME.EXE" and machine.read16(ds + 0x3dc8) != rows[-1]["stopped"]:
                        rows.append(dict(clock=machine.clock, seconds=(machine.clock - flight_start) / machine.ips,
                                         **strike_state(machine)))
            elif machine.run_until(machine.clock + step) != Machine.SLICE: break
        machine.screen(out / "final.ppm")
        failures = errors(rows, args.complete)
        if args.complete and flight_block:
            landed_report = dict(mission_result=machine.read16(flight_block + 0x28),
                                 pilot_status=machine.read16(flight_block + 0x26))
            failures.extend(landing_errors(rows, landed_report, (out / "run.log").read_text()))
        report = dict(clock=machine.clock, hash=f"{machine.hash:016x}", program=machine.program,
                      errors=failures, observation=rows[-1] if rows else None, **landed_report)
        (out / "result.json").write_text(json.dumps(report, indent=2) + "\n")
        with (out / "flight.csv").open("w", newline="") as stream:
            if rows:
                writer = csv.DictWriter(stream, fieldnames=rows[0]); writer.writeheader(); writer.writerows(rows)
        print(json.dumps(report), flush=True)
    return int(bool(report["errors"]))


if __name__ == "__main__": raise SystemExit(main())
