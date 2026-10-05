#!/usr/bin/env python3
"""Observe a normal supply drop; require timely impact and original D5 no-credit.

Reads guest fields and replays keys/mouse only. A consumed store, TTL expiry,
or crate hovering at altitude one is insufficient evidence of delivery.
"""
import argparse
import csv
import json
from pathlib import Path
import re
import tempfile

from machine_api import Machine
from landing_pilot import signed
from strike_pilot import strike_state


def observe(machine, slot=None):
    ds = (machine.psp + 0x10 + 0x1e42) << 4
    state = strike_state(machine)
    del state["stations"]
    state["strip_events"] = sum(machine.read8(ds + 0xba5a + i * 6) == 0x8b
        for i in range(min(state["event_count"], 255)))
    if slot is None:
        slot = next((i for i in range(8, 12)
            if machine.read16(ds + 0x3c3a + i * 28 + 16) == 0x26
            and machine.read16(ds + 0x3c3a + i * 28 + 14) > 0), None)
    state.update(cargo_slot=-1, cargo_type=0, cargo_x=0, cargo_y=0,
                 cargo_z=0, cargo_ttl=0, cargo_speed=0, cargo_pitch=0,
                 cargo_owner=0, cargo_weapon=0)
    if slot is not None:
        base = ds + 0x3c3a + slot * 28
        for name, offset in (("x", 0), ("y", 2), ("z", 4), ("speed", 6),
                             ("pitch", 10), ("ttl", 14), ("type", 16),
                             ("weapon", 18), ("owner", 22)):
            word = machine.read16(base + offset)
            state["cargo_" + name] = signed(word) if name in ("z", "speed", "pitch", "owner") else word
        state["cargo_slot"] = slot
    state.update(impact_x=machine.read16(ds + 0xc0cc),
                 impact_y=machine.read16(ds + 0xc0d6),
                 impact_z=signed(machine.read16(ds + 0xc0dc)),
                 mission_time=signed(machine.read16(ds + 0x9912)),
                 deadline=signed(machine.read16(ds + 0xdeb4)))
    return state


def errors(rows):
    if not rows:
        return ["no observed flight"]
    first, last = rows[0], rows[-1]
    failures = []
    if any(r["objective_type"] != 3 for r in rows):
        failures.append("not a supply-drop primary")
    if not any(r["agl"] > r["ground"] + 100 for r in rows) or last["agl"] <= last["ground"]:
        failures.append("no airborne flight or aircraft not airborne after impact")
    if first["weapon"] != 18 or first["store_count"] != 1 or last["store_count"] != 0:
        failures.append("expected one consumed cargo store")
    if first["launch_events"] != 0 or last["launch_events"] != 1:
        failures.append("expected one normal release event")
    if any(r["ejection"] or r["fuel"] <= 0 or r["target_damaged"] for r in rows):
        failures.append("loss state, exhausted fuel or damaged delivery target")
    if any(r["flags"] & 0x4000 or r["strip_events"] for r in rows):
        failures.append("original D5 unexpectedly awarded primary delivery credit")
    impacts = [(a, b) for a, b in zip(rows, rows[1:])
        if a["cargo_ttl"] > 20 and b["cargo_ttl"] == 0
        and a["cargo_slot"] == b["cargo_slot"]]
    if len(impacts) != 1:
        failures.append("expected one impact, distinct from normal TTL expiry")
    else:
        before, impact = impacts[0]
        if last["clock"] - impact["clock"] < 9_000_000:
            failures.append("no full second of retained no-credit state after impact")
        if not (8 <= impact["cargo_slot"] < 12 and before["cargo_type"] == impact["cargo_type"] == 0x26
                and before["cargo_weapon"] == impact["cargo_weapon"] == 18):
            failures.append("impact does not belong to the player's released cargo")
        if (impact["impact_x"], impact["impact_y"], impact["impact_z"]) != (
                impact["cargo_x"], impact["cargo_y"], impact["cargo_z"]):
            failures.append("impact globals do not match the tracked cargo")
        # Require penetration rather than inferring an obstacle from a TTL change.
        if impact["cargo_z"] >= 0:
            failures.append("cargo did not cross ground altitude")
        dx = abs(signed(impact["cargo_x"] - impact["target_x"]))
        dy = abs(signed(impact["cargo_y"] - impact["target_y"]))
        if max(dx, dy) + min(dx, dy) // 2 >= 256:
            failures.append("impact is outside the original delivery area")
        if impact["mission_time"] >= impact["deadline"]:
            failures.append("impact missed the original delivery deadline")
    return failures


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--data", required=True)
    parser.add_argument("--replay", type=Path, required=True)
    parser.add_argument("--steps", type=int, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--engine", choices=("interp", "recomp"), default="recomp")
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
    rows, position, slot = [], 0, None
    with Machine(args.data, tempfile.mkdtemp(dir=args.out), engine=args.engine,
                 time_us=int(header[1]), log=args.out / "run.log") as machine:
        machine.record(args.out / "input.log")
        while machine.clock < args.steps:
            while position < len(replay) and int(replay[position][1]) < machine.clock + machine.ips:
                p = replay[position]
                if p[0] == "K": machine.key(int(p[1]), int(p[2], 16))
                else: machine.mouse(int(p[1]), *map(int, p[2:5]))
                position += 1
            step = 90_000
            if machine.program == "VGAME.EXE" and machine.clock - machine.start > 190_000_000:
                state = observe(machine, slot)
                if state["cargo_slot"] >= 0: slot = state["cargo_slot"]
                rows.append(dict(clock=machine.clock, **state))
                step = machine.ips // 5
            if machine.run_until(min(args.steps, machine.clock + step)) != Machine.SLICE:
                break
        if machine.program == "VGAME.EXE":
            rows.append(dict(clock=machine.clock, **observe(machine, slot)))
        report = dict(clock=machine.clock, hash=f"{machine.hash:016x}", program=machine.program,
                      errors=errors(rows), observation=rows[-1] if rows else None)
        if machine.program != "VGAME.EXE":
            report["errors"].append("flight exited before the cargo observation")
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
