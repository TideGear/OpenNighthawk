#!/usr/bin/env python3
"""Fly a generated photo objective through normal controls and record it.

The first diagnostic stops after earning primary credit; it does not claim
a completed return or a successful sortie. Observations never write RAM.
"""
import argparse
import csv
import json
import math
import tempfile
from pathlib import Path

from landing_pilot import observe, signed, clamp, control as landing_control, landing_errors
from machine_api import Machine, RouteInputs
from run_route import route_args


def recon_state(machine):
    state = observe(machine)
    ds = (machine.psp + 0x10 + 0x1E42) << 4
    offsets = {"objective_type": 0xE304, "target": 0xE306, "photos": 0x4220,
               "shutter": 0x4222, "lock": 0x3D94, "cue": 0xB2B8,
               "display": 0xE008, "mode": 0xE32A, "station": 0x3680,
               "bay_switch": 0xE58A, "event_count": 0x951A}
    state.update({name: machine.read16(ds + offset) for name, offset in offsets.items()})
    state["bay_switch"] = machine.read8(ds + 0xE58A)
    state["weapon"] = machine.read16(ds + 0x3670 + state["station"] * 4)
    state["store_count"] = machine.read16(ds + 0x3672 + state["station"] * 4)
    target = ds + 0xB2CE + state["target"] * 16
    state["target_x"] = machine.read16(target + 2)
    state["target_y"] = machine.read16(target + 4)
    state["target_damaged"] = int(bool(machine.read8(target + 8) & 0x80))
    state["secondary_type"] = machine.read16(ds + 0xE316)
    state["secondary_target"] = machine.read16(ds + 0xE318)
    secondary = ds + 0xB2CE + state["secondary_target"] * 16
    state["secondary_damaged"] = int(bool(machine.read8(secondary + 8) & 0x80))
    state["target_range"] = math.hypot(signed(state["target_x"] - state["x"]),
                                       signed(state["target_y"] - state["y"]))
    state["credit_events"] = sum(
        machine.read8(ds + 0xBA5A + i * 6) == 0x8A
        and machine.read8(ds + 0xBA5B + i * 6) == state["target"]
        for i in range(min(state["event_count"], 255)))
    state["secondary_credit_events"] = sum(
        machine.read8(ds + 0xBA5A + i * 6) == 0x4A
        and machine.read8(ds + 0xBA5B + i * 6) == state["secondary_target"]
        for i in range(min(state["event_count"], 255)))
    return state


def control(machine, state, tick):
    at = machine.clock + 1
    dx, dy = signed(state["target_x"] - state["x"]), signed(state["target_y"] - state["y"])
    heading = math.atan2(dx, -dy) * 32768 / math.pi
    bank = clamp(signed(int(heading) - state["heading"]) * 1.5, -6000, 6000)
    roll_error = bank - state["roll"]
    want_pitch = clamp((2500 - state["altitude"]) * 2, -1000, 1800) + state["trim"]
    designated = state["lock"] != 0xFFFF and state["lock"] & 0x7F == state["target"]
    if designated and state["target_range"] < 1500:
        want_pitch = -math.atan2(state["altitude"], state["target_range"] * 32) * 32768 / math.pi + 0x6EF
    pitch_error = want_pitch - state["pitch"]
    if abs(pitch_error) > 200:
        machine.type(at, r"\D" if pitch_error > 0 else r"\U", hold_ms=60)
    if abs(roll_error) > 300:
        machine.type(at + machine.ips // 10, r"\R" if roll_error > 0 else r"\L", hold_ms=60)
    command = None
    if tick % 10 == 0:
        if not state["flags"] & 1:
            command = "6"
        elif state["display"] != 0x13:
            command = "/"
        elif state["mode"] != 2:
            command = r"\2"
        elif state["weapon"] != 16:
            command = r"\s"
        elif not state["bay_switch"]:
            command = "8"
        elif state["target_range"] < 1500 and not designated and not state["flags"] & 0x100:
            command = "n"
        elif designated and state["cue"] & 1 and not state["photos"]:
            command = r"\r"
        elif state["target_range"] < 1500 and state["throttle"] > 60:
            command = "-"
        if command:
            machine.type(at + machine.ips * 17 // 100, command, hold_ms=20)


def recon_errors(rows, complete=False):
    if not rows or not any(s["agl"] > s["ground"] + 100 for s in rows):
        return ["no airborne flight recorded"]
    last = rows[-1]
    errors = []
    if last["objective_type"] != 1:
        errors.append("primary objective is not reconnaissance")
    required_flags = 0x6000 if complete else 0x4000
    if last["flags"] & required_flags != required_flags:
        errors.append("primary objective credit is missing")
    if last["photos"] != (2 if complete else 1) or last["credit_events"] != 1:
        errors.append("expected one exposure and one primary photo-credit event")
    if last["target_damaged"] or last["ejection"]:
        errors.append("target damage or ejection/crash state")
    if last["weapon"] != 16 or last["store_count"] < 1:
        errors.append("camera is not retained")
    if complete and (last["secondary_type"] != 1 or last["secondary_credit_events"] != 1 or last["secondary_damaged"]):
        errors.append("secondary photo event or intact reconnaissance target is missing")
    return errors


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--data", required=True)
    parser.add_argument("--front-route", default=str(Path(__file__).parent / "routes" / "recon.front"))
    parser.add_argument("--replay", help="observe a recorded flight instead of controlling")
    parser.add_argument("--out", required=True)
    parser.add_argument("--engine", choices=("interp", "recomp"), default="recomp")
    parser.add_argument("--seconds", type=int, default=1800)
    parser.add_argument("--complete", action="store_true", help="also attempt secondary photo and home return")
    args = parser.parse_args()
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    inputs = RouteInputs(route_args(args.front_route)) if not args.replay else None
    rows, tick, initialized, start, last, writer = [], 0, False, None, "", None
    approach, flight_block, landed_report = False, None, {}
    with Machine(args.data, tempfile.mkdtemp(prefix="save-", dir=out),
                 log=out / "run.log", engine=args.engine) as machine, \
            (out / "flight.csv").open("w", newline="") as csvfile:
        machine.record(out / "input.log")
        replay, replay_pos = [], 0
        if args.replay:
            lines = Path(args.replay).read_text().splitlines()
            if not lines or lines[0] != "# f117r-input ips=9000000 time_us=700000000000000":
                raise ValueError("replay requires the route's fixed clock speed and boot time")
            for line in lines[1:]:
                parts = line.split()
                if not parts or parts[0].startswith("#"):
                    continue
                if parts[0] != "K" and not (parts[0] == "M" and parts[5:] == ["0", "0"]):
                    raise ValueError("replay supports recorded keys and absolute mouse input")
                replay.append(parts)
        while machine.clock < 6_000_000_000 + args.seconds * machine.ips:
            if inputs:
                inputs.poll(machine)
            else:
                while replay_pos < len(replay) and int(replay[replay_pos][1]) < machine.clock + machine.ips:
                    parts = replay[replay_pos]
                    if parts[0] == "K":
                        machine.key(int(parts[1]), int(parts[2], 16))
                    else:
                        machine.mouse(int(parts[1]), *map(int, parts[2:5]))
                    replay_pos += 1
            if machine.program != last:
                last = machine.program
                print(machine.clock, last, flush=True)
            step = 90_000
            if last == "VGAME.EXE":
                start = machine.start
                elapsed = machine.clock - start
                if elapsed > args.seconds * machine.ips:
                    break
                if not initialized and elapsed > 30_000_000:
                    state = recon_state(machine)
                    if not args.replay and state["flags"] & 8:
                        machine.type(start + 80_000_000, "0")
                    if not args.replay:
                        machine.type(start + 100_000_000, "+")
                        machine.type(start + 170_000_000, r"\D", hold_ms=1000)
                    initialized = True
                if elapsed > 190_000_000:
                    state = recon_state(machine)
                    row = {"clock": machine.clock, "seconds": elapsed / machine.ips, **state}
                    if writer is None:
                        writer = csv.DictWriter(csvfile, fieldnames=list(row))
                        writer.writeheader()
                    writer.writerow(row)
                    rows.append(row)
                    if args.complete and state["flags"] & 0x6000 == 0x6000 and not (out / "both-photos.ppm").exists():
                        machine.screen(out / "both-photos.ppm")
                    if args.complete and state["box"] and state["nearest"] == state["home"] and state["agl"] == max(state["ground"], state["surface"]):
                        shot = out / ("stopped.ppm" if state["speed"] <= 1 else "touchdown.ppm")
                        if not shot.exists(): machine.screen(shot)
                    if tick % 50 == 0:
                        csvfile.flush()
                        print({k: round(row[k], 1) for k in ("seconds", "target_range", "range", "altitude", "speed", "pitch", "lock", "cue", "photos", "credit_events", "fuel")}, flush=True)
                    if state["flags"] & 0x4000 and not args.complete:
                        machine.screen(out / "credit.ppm")
                        # Let all key releases retire before ending the record.
                        machine.run_until(machine.clock + machine.ips)
                        break
                    if not args.replay or (args.complete and replay_pos == len(replay) and machine.clock > int(replay[-1][1])):
                        if args.complete and state["flags"] & 0x6000 == 0x6000:
                            waypoint_range = math.hypot(signed(state["home_x"] - state["x"]),
                                signed(state["home_y"] + 4000 - state["y"]))
                            if waypoint_range < 150: approach = True
                            landing_control(machine, state, tick, approach)
                        elif args.complete and state["flags"] & 0x4000:
                            ds = (machine.psp + 0x10 + 0x1E42) << 4
                            secondary = machine.read16(ds + 0xE318)
                            target = ds + 0xB2CE + secondary * 16
                            working = {**state, "target": secondary, "photos": 0,
                                "target_x": machine.read16(target + 2), "target_y": machine.read16(target + 4),
                                "cue": state["cue"] >> 1}
                            working["target_range"] = math.hypot(signed(working["target_x"] - state["x"]),
                                signed(working["target_y"] - state["y"]))
                            control(machine, working, tick)
                            if tick % 10 == 5 and working["target_range"] < 1500 and state["lock"] != 0xFFFF and state["lock"] & 0x7F != secondary:
                                machine.type(machine.clock + 1, "b", hold_ms=20)
                        else:
                            control(machine, state, tick)
                    flight_block = state["flight_block"]
                    tick += 1
                    step = machine.ips // 5
            elif start is not None:
                break
            if machine.run_until(machine.clock + step) != Machine.SLICE:
                break
        machine.screen(out / "final.ppm")
        if machine.program == "VGAME.EXE" and rows:
            final_state = recon_state(machine)
            final_row = {"clock": machine.clock, "seconds": (machine.clock - machine.start) / machine.ips,
                         **final_state}
            writer.writerow(final_row)
            rows.append(final_row)
        if args.complete and flight_block:
            landed_report = {"mission_result": machine.read16(flight_block + 0x28),
                             "pilot_status": machine.read16(flight_block + 0x26)}
        errors = recon_errors(rows, args.complete)
        if args.complete:
            errors.extend(landing_errors(rows, landed_report, (out / "run.log").read_text()))
        report = {"clock": machine.clock, "hash": f"{machine.hash:016x}",
                  "program": machine.program, "errors": errors, **landed_report}
        if rows:
            report["observation"] = rows[-1]
            report["credit_observation"] = next((s for s in rows if s["credit_events"] == 1), None)
            report["secondary_credit_observation"] = next((s for s in rows if s["secondary_credit_events"] == 1), None)
        (out / "result.json").write_text(json.dumps(report, indent=2) + "\n")
        print(json.dumps(report), flush=True)
    return int(bool(report["errors"]))


if __name__ == "__main__":
    raise SystemExit(main())
