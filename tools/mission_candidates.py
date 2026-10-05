#!/usr/bin/env python3
"""Find generated objectives by varying startup clock and briefing timing.

Records normal input and reads the generated mission; never changes guest
memory, RNG state or roster statistics. Each case's startup clock is recorded
in the input header. Outputs stay in a private directory.
"""
import argparse
import json
from pathlib import Path
import tempfile

from machine_api import Machine, RouteInputs
from run_route import route_args
from strike_pilot import strike_state


def delayed_front(args, delay, after):
    result = list(args)
    for i in range(0, len(result), 2):
        if result[i] not in ("--type", "--click", "--move"):
            continue
        when, content = result[i + 1].split(":", 1)
        if when.startswith("START.EXE+"):
            clock = int(when.split("+", 1)[1])
            if clock >= after:
                result[i + 1] = f"START.EXE+{clock + delay}:{content}"
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--data", required=True)
    parser.add_argument("--route", required=True, help="frontend-only ordinary input route")
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--delays", default="0,90000000,180000000,270000000")
    parser.add_argument("--after", type=int, default=1100000000)
    parser.add_argument("--time-us", type=int, default=700000000000000)
    parser.add_argument("--time-step-us", type=int, default=1000000,
                        help="advance the emulated startup clock per case; zero isolates input timing")
    parser.add_argument("--types", help="stop after either objective has one of these comma-separated types")
    args = parser.parse_args()
    wanted = set(map(int, args.types.split(","))) if args.types else set()
    args.out.mkdir(parents=True, exist_ok=False)
    reports = []
    for case, delay in enumerate(map(int, args.delays.split(","))):
        out = args.out / str(delay)
        out.mkdir()
        inputs = RouteInputs(delayed_front(route_args(args.route), delay, args.after))
        time_us = args.time_us + case * args.time_step_us
        report = dict(delay=delay, time_us=time_us, error="flight not reached before budget")
        with Machine(args.data, tempfile.mkdtemp(prefix="save-", dir=out),
                     log=out / "run.log", engine="recomp", time_us=time_us) as machine:
            machine.record(out / "input.log")
            while machine.clock < 8_000_000_000 + delay:
                inputs.poll(machine)
                if machine.program == "VGAME.EXE" and machine.clock - machine.start > 40_000_000:
                    report = dict(delay=delay, time_us=time_us, clock=machine.clock,
                                  hash=f"{machine.hash:016x}", **strike_state(machine))
                    machine.screen(out / "mission.ppm")
                    break
                if machine.run_until(machine.clock + 90_000) != Machine.SLICE:
                    break
        (out / "result.json").write_text(json.dumps(report, indent=2) + "\n")
        reports.append(report)
        (args.out / "candidates.json").write_text(json.dumps(reports, indent=2) + "\n")
        print(json.dumps(report), flush=True)
        if wanted.intersection((report.get("objective_type"), report.get("secondary_type"))):
            break
    return int(any("error" in report for report in reports))


if __name__ == "__main__":
    raise SystemExit(main())
