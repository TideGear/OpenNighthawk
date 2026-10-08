#!/usr/bin/env python3
"""Fly a generated photo objective through normal controls and record it.

The first diagnostic stops after earning primary credit; it does not claim
a completed return or a successful sortie. Observations never write RAM.
"""
import argparse
import csv
import json
import math
import re
import shutil
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


def control(machine, state, tick, *, acquisition="nose", select_key="n",
            select_range=1500, aim_range=1500, camera_aim=True,
            pitch_tolerance=200):
    at = machine.clock + 1
    dx, dy = signed(state["target_x"] - state["x"]), signed(state["target_y"] - state["y"])
    heading = math.atan2(dx, -dy) * 32768 / math.pi
    bank = clamp(signed(int(heading) - state["heading"]) * 1.5, -6000, 6000)
    roll_error = bank - state["roll"]
    want_pitch = clamp((2500 - state["altitude"]) * 2, -1000, 1800) + state["trim"]
    designated = state["lock"] != 0xFFFF and state["lock"] & 0x7F == state["target"]
    # N casts a ray along the physical nose (recon_prepare). The photo
    # cue separately includes the camera's 0x6EF mounting offset. Applying
    # that offset before acquisition can aim N beyond a nearby target.
    if state["target_range"] < 1500 and acquisition == "level" and not designated:
        # The original uses a constant 640-map-unit ray for nonnegative
        # nose pitch. This avoids the coarse sin(angle,32)+1 divisor while
        # acquiring a target; the normal camera angle follows acquisition.
        want_pitch = clamp(want_pitch, 300, 1200)
    elif state["target_range"] < aim_range:
        want_pitch = -math.atan2(state["altitude"], state["target_range"] * 32) * 32768 / math.pi
        if designated and camera_aim:
            want_pitch += 0x6EF
    pitch_error = want_pitch - state["pitch"]
    if abs(pitch_error) > pitch_tolerance:
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
        elif (state["target_range"] < select_range and not designated and not state["flags"] & 0x100
              and (acquisition != "level" or (550 <= state["target_range"] <= 750
                   and state["pitch"] >= 0 and abs(signed(int(heading) - state["heading"])) < 1200))):
            command = select_key
        elif designated and state["cue"] & 1 and not state["photos"]:
            command = r"\r"
        elif state["speed"] < 240 and state["throttle"] < 75:
            command = "="
        elif state["target_range"] < 1500 and state["speed"] > 320 and state["throttle"] > 60:
            command = "-"
        if command:
            machine.type(at + machine.ips * 17 // 100, command, hold_ms=20)


EXTEND_WITHIN = 1200      # a secondary target closer than this and 45 degrees or more off the nose: extend
EXTEND_TO = 3000          # out to this range before turning in again


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
    parser.add_argument("--initial-roster", type=Path, help="continue from an earned roster file in a fresh private save directory")
    parser.add_argument("--time-us", type=int, default=700000000000000,
                        help="emulated startup clock; replay uses its recorded header")
    parser.add_argument("--extend", action="store_true",
                        help="with --complete: fly straight out and re-attack a secondary target the turn cannot reach")
    parser.add_argument("--cycle", action="store_true",
                        help="with --complete: while another object is designated, select the secondary with b only")
    parser.add_argument("--primary-only", action="store_true", help="with --complete, go home after the primary photo")
    parser.add_argument("--debrief", action="store_true", help="after the flight, take END's screens (writes ROSTER.FIL)")
    parser.add_argument("--deck-pitch-floor", type=int, default=None,
                        help="the lowest pitch commanded on a raised-deck approach (none: the original recipe)")
    parser.add_argument("--landing-throttle-gain", type=float, default=.1,
                        help="throttle per knot short of the approach speed (landing_pilot.control)")
    parser.add_argument("--landing-aim", type=int, default=20,
                        help="how far before the runway centre the glide path meets the ground")
    parser.add_argument("--approach-speed", type=int, default=200,
                        help="speed held on the final approach")
    parser.add_argument("--acquisition", choices=("nose", "level"), default="nose",
                        help="normal target designation approach for adaptive controls")
    args = parser.parse_args()
    lines = []
    if args.replay:
        lines = Path(args.replay).read_text().splitlines()
        header = re.fullmatch(r"# f117r-input ips=9000000 time_us=(\d+)", lines[0] if lines else "")
        if not header:
            raise ValueError("replay requires a recorded 9 MHz startup clock")
        args.time_us = int(header[1])
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    inputs = RouteInputs(route_args(args.front_route)) if not args.replay else None
    rows, tick, initialized, start, last, writer = [], 0, False, None, "", None
    approach, flight_block, landed_report = False, None, {}
    extending = False
    save = Path(tempfile.mkdtemp(prefix="save-", dir=out))
    if args.initial_roster:
        roster = args.initial_roster.read_bytes()
        if len(roster) != 802:
            raise ValueError("initial roster must be the original 802-byte saved file")
        shutil.copyfile(args.initial_roster, save / "Roster.Fil")
    with Machine(args.data, save,
                 log=out / "run.log", engine=args.engine, time_us=args.time_us) as machine, \
            (out / "flight.csv").open("w", newline="") as csvfile:
        machine.record(out / "input.log")
        replay, replay_pos = [], 0
        if args.replay:
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
                        done = 0x4000 if args.primary_only else 0x6000
                        if args.complete and state["flags"] & done == done:
                            waypoint_range = math.hypot(signed(state["home_x"] - state["x"]),
                                signed(state["home_y"] + 4000 - state["y"]))
                            if waypoint_range < 150: approach = True
                            landing_control(machine, state, tick, approach, deck_aim=300,
                                            aim=args.landing_aim, approach_speed=args.approach_speed,
                                            throttle_gain=args.landing_throttle_gain,
                                            deck_pitch_floor=args.deck_pitch_floor)
                        elif args.complete and state["flags"] & 0x4000:
                            ds = (machine.psp + 0x10 + 0x1E42) << 4
                            secondary = machine.read16(ds + 0xE318)
                            target = ds + 0xB2CE + secondary * 16
                            working = {**state, "target": secondary, "photos": 0,
                                "target_x": machine.read16(target + 2), "target_y": machine.read16(target + 4),
                                "cue": state["cue"] >> 1}
                            working["target_range"] = math.hypot(signed(working["target_x"] - state["x"]),
                                signed(working["target_y"] - state["y"]))
                            # Extend and re-attack: a target inside the turning circle (about 560 units at
                            # 350 knots) stays abeam, never in the camera's view, and the navigator orbits
                            # it. Close and well off the nose, fly straight out to EXTEND_TO, then turn in.
                            bearing = math.atan2(signed(working["target_x"] - state["x"]),
                                                 -signed(working["target_y"] - state["y"])) * 32768 / math.pi
                            off_nose = abs(signed(int(bearing) - state["heading"]))
                            if args.extend and working["target_range"] < EXTEND_WITHIN and off_nose > 8192:
                                extending = True
                            if extending and working["target_range"] > EXTEND_TO:
                                extending = False
                            if extending:
                                theta = state["heading"] * math.pi / 32768
                                working = {**working, "target_x": int(state["x"] + 5000 * math.sin(theta)) & 0xFFFF,
                                           "target_y": int(state["y"] - 5000 * math.cos(theta)) & 0xFFFF,
                                           "target_range": 5000.0}
                            # With another object designated, step on with b only: N from the nose
                            # can pick the same wrong object again each time, and b's next press
                            # then starts over (a Veteran sortie alternated 22 and 0 for 500 s).
                            wrong_lock = state["lock"] != 0xFFFF and state["lock"] & 0x7F != secondary
                            control(machine, working, tick, acquisition=args.acquisition,
                                    select_range=0 if args.cycle and wrong_lock else 1500)
                            if tick % 10 == 5 and working["target_range"] < 1500 and state["lock"] != 0xFFFF and state["lock"] & 0x7F != secondary:
                                machine.type(machine.clock + 1, "b", hold_ms=20)
                        else:
                            control(machine, state, tick, acquisition=args.acquisition)
                    flight_block = state["flight_block"]
                    tick += 1
                    step = machine.ips // 5
            elif start is not None:
                if args.debrief:
                    # END's screens with the career routes' keys until START is back; END writes the
                    # sortie into the roster on the way (strike_pilot.END_KEYS).
                    from strike_pilot import END_KEYS
                    end_inputs = RouteInputs(END_KEYS)
                    limit = machine.clock + 3_000_000_000
                    while machine.clock < limit and machine.program.upper() != "START.EXE":
                        end_inputs.poll(machine)
                        if machine.run_until(machine.clock + 90_000) != Machine.SLICE:
                            break
                    machine.run_until(machine.clock + 600_000_000)                # START writes the roster once back
                    print(machine.clock, machine.program, "after the debriefing", flush=True)
                break
            if args.complete and rows and rows[-1]["box"] and rows[-1]["nearest"] == rows[-1]["home"] and rows[-1]["speed"] <= 1 and rows[-1]["throttle"] == 0:
                # Observe the completed countdown before DOS exit lets the
                # next executable reuse VGAME's memory. Keep the strict gate.
                until = machine.clock + step
                ds = (machine.psp + 0x10 + 0x1e42) << 4
                while machine.clock < until and machine.program == "VGAME.EXE":
                    if machine.run_until(min(until, machine.clock + 128)) != Machine.SLICE:
                        break
                    if machine.program == "VGAME.EXE" and machine.read16(ds + 0x3dc8) != rows[-1]["stopped"]:
                        row = dict(clock=machine.clock, seconds=(machine.clock - start) / machine.ips,
                                   **recon_state(machine))
                        rows.append(row)
                        writer.writerow(row)
            elif machine.run_until(machine.clock + step) != Machine.SLICE:
                break
        machine.screen(out / "final.ppm")
        if args.debrief:
            for f in save.rglob("*"):
                if f.name.upper() == "ROSTER.FIL":
                    shutil.copyfile(f, out / "ROSTER.FIL")
        if machine.program == "VGAME.EXE" and rows:
            final_state = recon_state(machine)
            final_row = {"clock": machine.clock, "seconds": (machine.clock - machine.start) / machine.ips,
                         **final_state}
            writer.writerow(final_row)
            rows.append(final_row)
        if args.complete and flight_block:
            landed_report = {"mission_result": machine.read16(flight_block + 0x28),
                             "pilot_status": machine.read16(flight_block + 0x26)}
        errors = recon_errors(rows, args.complete and not args.primary_only)
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
