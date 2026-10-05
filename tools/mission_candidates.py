#!/usr/bin/env python3
"""Find generated objectives by varying startup clock and briefing timing.

Records normal input and reads the generated mission; never changes guest
memory, RNG state or roster statistics. Each case's startup clock is recorded
in the input header. Outputs stay in a private directory.
"""
import argparse
import hashlib
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
    matching = parser.add_mutually_exclusive_group()
    matching.add_argument("--primary-only", action="store_true", help="apply --types only to the primary objective")
    matching.add_argument("--both-objectives", action="store_true", help="require both objectives to match --types")
    parser.add_argument("--initial-roster", type=Path, help="copy an existing saved roster unchanged into each fresh case")
    parser.add_argument("--fix", action="append", default=[], help="switch a fix on (docs/bugs.md), e.g. D4")
    args = parser.parse_args()
    wanted = set(map(int, args.types.split(","))) if args.types else set()
    roster = args.initial_roster.read_bytes() if args.initial_roster else None
    if roster is not None and len(roster) != 802:
        raise ValueError("initial roster must be the original 802-byte saved file")
    args.out.mkdir(parents=True, exist_ok=False)
    reports = []
    for case, delay in enumerate(map(int, args.delays.split(","))):
        out = args.out / str(delay)
        out.mkdir()
        inputs = RouteInputs(delayed_front(route_args(args.route), delay, args.after))
        time_us = args.time_us + case * args.time_step_us
        report = dict(delay=delay, time_us=time_us, error="flight not reached before budget")
        save = Path(tempfile.mkdtemp(prefix="save-", dir=out))
        if roster is not None:
            (save / "Roster.Fil").write_bytes(roster)
        with Machine(args.data, save, log=out / "run.log", engine="recomp",
                     time_us=time_us, fixes=args.fix) as machine:
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
        if roster is not None:
            report["initial_roster_sha256"] = hashlib.sha256(roster).hexdigest()
        (out / "result.json").write_text(json.dumps(report, indent=2) + "\n")
        reports.append(report)
        (args.out / "candidates.json").write_text(json.dumps(reports, indent=2) + "\n")
        print(json.dumps(report), flush=True)
        objectives = (report.get("objective_type"),) if args.primary_only else (
            report.get("objective_type"), report.get("secondary_type"))
        matches = [kind in wanted for kind in objectives]
        if wanted and (all(matches) if args.both_objectives else any(matches)):
            break
    return int(any("error" in report for report in reports))


if __name__ == "__main__":
    raise SystemExit(main())
