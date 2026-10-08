#!/usr/bin/env python3
"""Earn successive photo sorties using normal recorded input and actual saves.

Each leg starts in a fresh directory with only the previous saved roster
copied unchanged. Both engines must pass strict flight/career checks and
agree in checkpoints, observations and all 802 saved bytes before proceeding.
Stops on failure; a recording may need replacing after a promotion changes
the generated assignment. All outputs, including saves, remain private.
"""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import re
import struct
import tempfile
from concurrent.futures import FIRST_COMPLETED, ProcessPoolExecutor, wait

from machine_api import Machine, RouteInputs
from recon_pilot import recon_state, recon_errors
from landing_pilot import landing_errors
from run_route import route_args


def career(roster, pilot=None):
    """The selected pilot's record, or `pilot`'s."""
    if len(roster) != 802:
        raise ValueError("requires an unchanged original 802-byte saved roster")
    if pilot is None:
        pilot = struct.unpack_from("<H", roster)[0]
    if pilot >= 10:
        raise ValueError("invalid selected pilot in the saved roster")
    base = 2 + pilot * 80
    return dict(pilot=pilot, rank=struct.unpack_from("<H", roster, base + 32)[0],
                score=struct.unpack_from("<H", roster, base + 48)[0],
                total=struct.unpack_from("<I", roster, base + 50)[0],
                sorties=struct.unpack_from("<H", roster, base + 54)[0],
                status=struct.unpack_from("<H", roster, base + 78)[0])


def career_errors(before, after):
    failures = []
    if before["status"] or before["sorties"] >= 99:
        failures.append("starting pilot is unavailable for another sortie")
    if after["pilot"] != before["pilot"] or after["sorties"] != before["sorties"] + 1:
        failures.append("selected pilot or earned sortie count did not advance normally")
    if not 0 < after["score"] < 0x8000 or after["total"] != before["total"] + after["score"]:
        failures.append("positive mission score was not added to the saved total")
    if after["status"] != (1 if after["sorties"] == 99 else 0):
        failures.append("unexpected active/retired career status")
    if after["rank"] < before["rank"]:
        failures.append("saved rank decreased")
    return failures


def leg(args, engine, previous, replay, time_us, out):
    out.mkdir()
    save = Path(tempfile.mkdtemp(prefix="save-", dir=out))
    (save / "Roster.Fil").write_bytes(previous)
    extra = RouteInputs(route_args(args.debrief_route))
    rows, hashes, position, flight_block, parent, fine = [], [], 0, None, None, False
    next_hash = 50_000_000
    failures = []
    with Machine(args.data, save, engine=engine, time_us=time_us, log=out / "run.log") as machine:
        machine.record(out / "input.log")
        while machine.clock < args.steps:
            while position < len(replay) and int(replay[position][1]) < machine.clock + machine.ips:
                p = replay[position]
                if p[0] == "K": machine.key(int(p[1]), int(p[2], 16))
                else: machine.mouse(int(p[1]), *map(int, p[2:5]))
                position += 1
            extra.poll(machine)
            step = 90_000
            if machine.program == "VGAME.EXE" and machine.clock - machine.start > 190_000_000:
                ds = (machine.psp + 0x10 + 0x1e42) << 4
                # Nearest-target fields change transiently during a frame's
                # search. Once stopped, retain fine sampling and read full
                # observations only when the completion counter changes.
                if not fine or machine.read16(ds + 0x3dc8) != rows[-1]["stopped"]:
                    state = recon_state(machine)
                    rows.append(dict(clock=machine.clock, **state))
                    flight_block = state["flight_block"]
                    if state["box"] and state["nearest"] == state["home"] and state["speed"] <= 1 and not state["throttle"]:
                        fine = True
                step = 128 if fine else machine.ips // 5
            elif flight_block is not None and parent is None:
                parent = dict(mission_result=machine.read16(flight_block + 0x28),
                              pilot_status=machine.read16(flight_block + 0x26))
                failures = recon_errors(rows, complete=True) + landing_errors(
                    rows, parent, (out / "run.log").read_text())
                machine.screen(out / "return.ppm")
                if failures: break
            limit = min(args.steps, machine.clock + step, next_hash)
            if machine.run_until(limit) != Machine.SLICE: break
            if machine.clock >= next_hash:
                hashes.append((machine.clock, f"{machine.hash:016x}", machine.program))
                next_hash += 50_000_000
        current = (save / "Roster.Fil").read_bytes()
        before, after = career(previous), career(current)
        if parent is None:
            failures.append("no accepted flight completion")
        failures.extend(career_errors(before, after))
        if machine.program != "START.EXE":
            failures.append("debriefing did not finish back in the frontend")
        report = dict(clock=machine.clock, hash=f"{machine.hash:016x}", program=machine.program,
                      initial_roster_sha256=hashlib.sha256(previous).hexdigest(),
                      saved_roster_sha256=hashlib.sha256(current).hexdigest(),
                      before=before, after=after, parent=parent, errors=failures)
        machine.screen(out / "final.ppm")
    (out / "save-path.txt").write_text(str(save.resolve()) + "\n")
    (out / "result.json").write_text(json.dumps(report, indent=2) + "\n")
    (out / "hashes.json").write_text(json.dumps(hashes) + "\n")
    with (out / "flight.csv").open("w", newline="") as stream:
        if rows:
            writer = csv.DictWriter(stream, fieldnames=rows[0])
            writer.writeheader(); writer.writerows(rows)
    print(engine, json.dumps(report), flush=True)
    return report, current


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--data", required=True)
    parser.add_argument("--initial-roster", type=Path, required=True,
                        help="actual saved roster from the preceding verified sortie")
    parser.add_argument("--replay", type=Path, required=True)
    parser.add_argument("--debrief-route", type=Path,
                        default=Path(__file__).parent / "routes" / "career_promotion.args",
                        help="normal END inputs; defaults to the promotion fixture")
    parser.add_argument("--steps", type=int, required=True)
    parser.add_argument("--count", type=int, default=1)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--ahead", type=int, default=0,
                        help="run up to N recompiled legs ahead of their interpreter legs, in parallel processes")
    args = parser.parse_args()
    if args.steps <= 0 or args.count <= 0 or args.ahead < 0:
        parser.error("--steps and --count must be positive, --ahead not negative")
    previous = args.initial_roster.read_bytes()
    first = career(previous)
    if first["status"] or first["sorties"] + args.count > 99:
        parser.error("the starting pilot must be active and the batch must end by sortie 99")
    lines = args.replay.read_text().splitlines()
    header = re.fullmatch(r"# f117r-input ips=9000000 time_us=(\d+)", lines[0] if lines else "")
    if not header:
        raise ValueError("requires a recorded 9 MHz startup clock")
    replay = [line.split() for line in lines[1:] if line and not line.startswith("#")]
    if not replay or any(not ((p[0] == "K" and len(p) == 3)
            or (p[0] == "M" and len(p) == 7 and p[5:] == ["0", "0"])) for p in replay):
        raise ValueError("requires ordinary recorded keys and absolute mouse only")
    args.out.mkdir(parents=True, exist_ok=False)
    numbers = range(first["sorties"] + 1, first["sorties"] + args.count + 1)
    if args.ahead:
        return pipelined(args, previous, replay, int(header[1]), numbers)
    summary = []
    for number in numbers:
        legs = [leg(args, engine, previous, replay, int(header[1]),
                    args.out / f"sortie-{number:02}-{engine}") for engine in ENGINES]
        if any(report["errors"] for report, _ in legs): return 1
        previous = accept(args, number, legs, summary)
    return 0


ENGINES = ("recomp", "interp")


def accept(args, number, legs, summary):
    """Both engines' legs of one sortie must agree in every artefact."""
    outputs = [args.out / f"sortie-{number:02}-{engine}" for engine in ENGINES]
    if legs[0][1] != legs[1][1]: raise ValueError("paired 802-byte career saves differ")
    for name in ("input.log", "flight.csv", "result.json", "hashes.json"):
        if (outputs[0] / name).read_bytes() != (outputs[1] / name).read_bytes():
            raise ValueError("paired career evidence differs: " + name)
    summary.append(legs[0][0])
    (args.out / "completed.json").write_text(json.dumps(summary, indent=2) + "\n")
    print("paired sortie", number, "flight/career gates and all checkpoints/save bytes agree", flush=True)
    return legs[0][1]


def pipelined(args, previous, replay, time_us, numbers):
    """The recompiled legs run ahead, each from the preceding recompiled
    save; every interpreter leg starts from the same bytes as its recompiled
    leg. Sorties are accepted strictly in order and only when both legs
    agree, so the accepted chain is exactly the sequential one: a later
    disagreement stops it at the last paired sortie."""
    out = lambda n, e: args.out / f"sortie-{n:02}-{e}"
    summary, inputs, recomp, interp = [], {}, {}, {}
    numbers = list(numbers)
    queue, accepted, running = list(numbers), 0, None
    with ProcessPoolExecutor(args.ahead + 1) as pool:
        while accepted < len(numbers):
            # recomp holds finished legs not yet accepted
            if running is None and queue and len(recomp) < args.ahead:
                n = queue.pop(0)
                inputs[n] = previous
                running = (n, pool.submit(leg, args, "recomp", previous, replay, time_us, out(n, "recomp")))
            futures = ([running[1]] if running else []) + list(interp.values())
            wait(futures, return_when=FIRST_COMPLETED)
            if running and running[1].done():
                n, future = running; running = None
                recomp[n] = future.result()
                if recomp[n][0]["errors"]: return 1
                previous = recomp[n][1]
                interp[n] = pool.submit(leg, args, "interp", inputs[n], replay, time_us, out(n, "interp"))
            n = numbers[accepted]
            if n in interp and interp[n].done():
                result = interp.pop(n).result()
                if result[0]["errors"]: return 1
                accept(args, n, [recomp.pop(n), result], summary)
                accepted += 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
