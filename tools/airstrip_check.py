#!/usr/bin/env python3
"""Read-only acceptance observer for a normally flown secret-airstrip landing.

The default checks primary credit at the strip; --complete also checks home return.
The supplied record contains only normal keys/mouse and its startup clock.
"""
import argparse
import csv
import json
from pathlib import Path
import re
import tempfile

from machine_api import Machine
from landing_pilot import signed, landing_errors
from strike_pilot import strike_state


def errors(rows, complete=False):
    if complete:
        # The strip must be intact at delivery. Enemy action afterwards
        # does not revoke an already earned original type-4 credit.
        delivered = next((i for i, r in enumerate(rows)
            if r["flags"] & 0x4000 and r["strip_events"] == 1 and not r["store_count"]
            and r["box"] == 1 and r["nearest"] == r["target"]
            and r["agl"] == r["ground"] and r["speed"] <= 1 and not r["throttle"]), None)
        if delivered is None:
            return ["no verified stop and delivery at the primary strip"]
        failures = errors(rows[:delivered + 1])
        after = rows[delivered + 1:]
        if not any(r["agl"] > max(r["ground"], r.get("surface", 0)) + 100 for r in after):
            failures.append("no second airborne leg after the strip delivery")
        if any(not r["flags"] & 0x4000 or r["strip_events"] != 1 or r["store_count"]
               or r["objective_type"] != 4 for r in after):
            failures.append("original strip credit or consumption was not retained")
        if any(r["ejection"] or r["fuel"] <= 0 for r in after):
            failures.append("loss state or exhausted fuel after delivery")
        return failures
    if not rows or not any(r["agl"] > r["ground"] + 100 for r in rows):
        return ["no airborne flight recorded"]
    first, last = rows[0], rows[-1]
    failures = []
    if first["objective_type"] != 4 or last["objective_type"] != 4:
        failures.append("not a secret-airstrip primary")
    if first["flags"] & 0x4000 or not last["flags"] & 0x4000:
        failures.append("primary credit was not earned")
    if first["strip_events"] != 0 or last["strip_events"] != 1:
        failures.append("expected one original primary strip event 8Bh")
    if first["store_count"] <= 0 or last["store_count"] != 0:
        failures.append("mission store was not consumed")
    if (last["box"] != 1 or last["nearest"] != last["target"]
        or abs(signed(last["x"] - last["target_x"])) > last["box_width"] >> 5
        or abs(signed(last["y"] - last["target_y"])) > last["box_length"] >> 5
        or last["agl"] != last["ground"]):
        failures.append("not on the ground inside the primary strip box")
    if last["speed"] > 1 or last["throttle"] or last["flags"] & 1 or not last["flags"] & 8:
        failures.append("not stopped at idle with gear down and brakes on")
    if any(r["ejection"] or r["fuel"] <= 0 or r["target_damaged"] for r in rows):
        failures.append("loss state, exhausted fuel or damaged strip")
    return failures


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--data", required=True)
    parser.add_argument("--replay", type=Path, required=True)
    parser.add_argument("--steps", type=int, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--engine", choices=("interp", "recomp"), default="recomp")
    parser.add_argument("--complete", action="store_true", help="also require a second airborne leg and normal home return")
    args = parser.parse_args()
    lines = args.replay.read_text().splitlines()
    header = re.fullmatch(r"# f117r-input ips=9000000 time_us=(\d+)", lines[0] if lines else "")
    if not header:
        raise ValueError("requires a recorded 9 MHz startup clock")
    replay = [line.split() for line in lines[1:] if line and not line.startswith("#")]
    if not replay or any(not ((p[0] == "K" and len(p) == 3)
                      or (p[0] == "M" and len(p) == 7 and p[5:] == ["0", "0"])) for p in replay):
        raise ValueError("requires ordinary recorded keys and absolute mouse only")
    args.out.mkdir(parents=True, exist_ok=False)
    rows, position, flight_block, fine = [], 0, None, False
    with Machine(args.data, tempfile.mkdtemp(dir=args.out), engine=args.engine,
                 time_us=int(header[1]), log=args.out / "run.log") as machine:
        machine.record(args.out / "input.log")
        while machine.clock < args.steps:
            while position < len(replay) and int(replay[position][1]) < machine.clock + machine.ips:
                p = replay[position]
                if p[0] == "K":
                    machine.key(int(p[1]), int(p[2], 16))
                else:
                    machine.mouse(int(p[1]), *map(int, p[2:5]))
                position += 1
            step = 90_000
            if machine.program == "VGAME.EXE" and machine.clock - machine.start > 190_000_000:
                ds = (machine.psp + 0x10 + 0x1e42) << 4
                if not fine or machine.read16(ds + 0x3dc8) != rows[-1]["stopped"]:
                    state = strike_state(machine)
                    state["strip_events"] = sum(machine.read8(ds + 0xba5a + i * 6) == 0x8b
                        for i in range(min(state["event_count"], 255)))
                    rows.append(dict(clock=machine.clock, **state))
                    flight_block = state["flight_block"]
                    if (args.complete and state["box"] and state["nearest"] == state["home"]
                            and state["speed"] <= 1 and not state["throttle"]):
                        fine = True
                step = 128 if fine else machine.ips // 5
            elif args.complete and flight_block is not None:
                break
            if machine.run_until(min(args.steps, machine.clock + step)) != Machine.SLICE:
                break
        if machine.program == "VGAME.EXE":
            ds = (machine.psp + 0x10 + 0x1e42) << 4
            state = strike_state(machine)
            state["strip_events"] = sum(machine.read8(ds + 0xba5a + i * 6) == 0x8b
                for i in range(min(state["event_count"], 255)))
            rows.append(dict(clock=machine.clock, **state))
        report = dict(clock=machine.clock, hash=f"{machine.hash:016x}", program=machine.program,
                      errors=errors(rows, args.complete), observation=rows[-1] if rows else None)
        if args.complete:
            if flight_block is not None:
                report.update(mission_result=machine.read16(flight_block + 0x28),
                              pilot_status=machine.read16(flight_block + 0x26))
            report["errors"].extend(landing_errors(rows, report, (args.out / "run.log").read_text()))
            report["post_delivery_target_damage"] = bool(rows and rows[-1]["target_damaged"])
        elif machine.program != "VGAME.EXE":
            report["errors"].append("flight exited before the strip observation")
        machine.screen(args.out / "final.ppm")
        (args.out / "result.json").write_text(json.dumps(report, indent=2) + "\n")
        with (args.out / "flight.csv").open("w", newline="") as stream:
            if rows:
                writer = csv.DictWriter(stream, fieldnames=rows[0])
                writer.writeheader(); writer.writerows(rows)
        print(json.dumps(report), flush=True)
    return int(bool(report["errors"]))


if __name__ == "__main__":
    raise SystemExit(main())
