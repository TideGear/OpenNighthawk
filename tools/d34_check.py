#!/usr/bin/env python3
"""Staged check of fix D34 (the destroyed-object table, docs/bugs.md).

Replays the strike route's normal input. With --stage, the table's count
[0x9932] is set to 30 once mission setup has written its records, so the
Maverick hit appends record 31; this one write is the run's only guest
write, and it makes the run a staged one. Unfixed, record 31 lands on
B834..B838 and its type byte on the F7/F8 flag at B838. With --fix D34 the
count stays 30, the flag at B838 keeps its value (B834..B837 are the
target camera's depth, which the game itself rewrites), and the run log
shows the record kept past the table and found by the game's own lookup.
"""
import argparse
import json
from pathlib import Path
import sys
import tempfile

from machine_api import Machine
from strike_pilot import strike_state

ROUTE = Path(__file__).parent / "routes" / "strike.input"
END = 8_797_594_941          # the strike route's own budget


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--data", required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--engine", choices=("interp", "recomp"), default="recomp")
    parser.add_argument("--fix", action="append", default=[])
    parser.add_argument("--stage", action="store_true", help="set the count to 30 after mission setup")
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=False)
    replay = [line.split() for line in ROUTE.read_text().splitlines()[1:] if line and not line.startswith("#")]
    position, before, events, previous = 0, None, [], None
    with Machine(args.data, tempfile.mkdtemp(dir=args.out), engine=args.engine,
                 log=args.out / "run.log", fixes=args.fix) as machine:
        while machine.clock < END:
            while position < len(replay) and int(replay[position][1]) < machine.clock + machine.ips:
                p = replay[position]
                if p[0] == "K": machine.key(int(p[1]), int(p[2], 16))
                else: machine.mouse(int(p[1]), *map(int, p[2:5]))
                position += 1
            if machine.program == "VGAME.EXE" and machine.clock - machine.start > 190_000_000:
                ds = (machine.psp + 0x10 + 0x1e42) << 4
                if before is None and machine.clock - machine.start > 1_500_000_000:
                    before = dict(clock=machine.clock, count=machine.read16(ds + 0x9932),
                                  tail=[machine.read8(ds + 0xB834 + k) for k in range(5)])
                    if args.stage:
                        machine.stage_write16(ds + 0x9932, 30)
                state = strike_state(machine)
                now = (machine.read16(ds + 0x9932), [machine.read8(ds + 0xB834 + k) for k in range(5)],
                       state["target_damaged"], state["hit_events"])
                if now != previous:
                    events.append(dict(clock=machine.clock, count=now[0], tail=now[1],
                                       target_damaged=now[2], hits=now[3]))
                    previous = now
            if machine.run_until(min(END, machine.clock + 900_000)) != Machine.SLICE:
                break
        ds = (machine.psp + 0x10 + 0x1e42) << 4
        state = strike_state(machine)
        log = (args.out / "run.log").read_text()
        report = dict(engine=args.engine, fixes=args.fix, staged=args.stage, before=before,
                      clock=machine.clock, hash=f"{machine.hash:016x}",
                      count=machine.read16(ds + 0x9932),
                      tail=[machine.read8(ds + 0xB834 + k) for k in range(5)],
                      target_damaged=state["target_damaged"], hits=state["hit_events"],
                      kept_past_table="[fix D34] destroyed-object record 31 kept past the table" in log,
                      found_past_table="[fix D34] lookup found record 31 past the table" in log,
                      events=events)
        machine.screen(args.out / "final.ppm")
    errors = []
    if before is None or not report["target_damaged"] or report["hits"] != 1:
        errors.append("the strike route did not reach its hit")
    fixed = "D34" in args.fix or "all" in args.fix
    if args.stage and fixed:
        if (report["count"] != 30 or report["tail"][4] != before["tail"][4]
                or any(e["count"] != 30 for e in events if e["clock"] > before["clock"])):
            errors.append("the fixed table overflowed into B834..B838")
        if not (report["kept_past_table"] and report["found_past_table"]):
            errors.append("record 31 was not kept past the table and found again")
    elif args.stage:
        if report["count"] != 31 or report["tail"][4] == before["tail"][4]:
            errors.append("the unfixed table did not overflow as the original's does")
    elif report["count"] >= 30 or report["kept_past_table"]:
        errors.append("an unstaged run reached the table's end")
    report["errors"] = errors
    (args.out / "result.json").write_text(json.dumps(report, indent=1) + "\n")
    print(json.dumps({k: report[k] for k in ("engine", "fixes", "staged", "count", "tail", "hash", "errors")}))
    return int(bool(errors))


if __name__ == "__main__":
    sys.exit(main())
