#!/usr/bin/env python3
"""stick_response.py: how much one stick tap moves the aircraft, on this machine, DOSBox-X or 86Box.

    py tools/stick_response.py --machine machine|dosbox-x|86box --data GOG_DIR --out DIR

The supply-drop route's front end (tools/routes/cargo_pilot.input) brings up VGAME; the aircraft takes
off with the pilots' first-minute keys (throttle up, a second of back stick), and from 40 s isolated
taps of the up arrow (nose down) and the left arrow are sent, each tap length several times, three
seconds apart, the pitch and roll read every 200 ms. The report is, per key and tap length, the change
from the tick before the tap to three ticks after it, and S ([0x368E], frames per second): the pilots'
key holds assume this machine's response, and the 86Box game runs at about half its frame rate.
"""
import argparse
import json
import os
import statistics
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from machine_api import Machine  # noqa: E402
from cargo_pilot import pilot_state  # noqa: E402

TAPS = [(key, ms) for ms in (20, 60, 120, 200, 400) for key in (r"\U", r"\L")] * 3


def run(machine, start):
    k = machine.ips / 9_000_000          # the clock constants below are the 9 MHz model's: scaled to seconds
    machine.type(start + int(100_000_000 * k), "+")
    machine.type(start + int(170_000_000 * k), r"\D", hold_ms=1000)
    first = start + int(400_000_000 * k)
    plan = [(first + int(i * 27_000_000 * k), key, ms) for i, (key, ms) in enumerate(TAPS)]
    rows = []
    while machine.clock < plan[-1][0] + int(40_000_000 * k):
        if machine.clock - start > int(190_000_000 * k):
            s = pilot_state(machine)
            rows.append((machine.clock, s["pitch"], s["roll"], s["S"], s["agl"]))
        for at, key, ms in plan:
            if machine.clock <= at < machine.clock + machine.ips // 5:
                machine.type(at, key, hold_ms=ms)
        if machine.run_until(machine.clock + machine.ips // 5) != Machine.SLICE:
            break
    out = {}
    for at, key, ms in plan:
        before = [r for r in rows if r[0] <= at]
        after = [r for r in rows if r[0] > at]
        if not before or len(after) < 4:
            continue
        b, a = before[-1], after[3]
        delta = (a[1] - b[1]) if key == r"\U" else (a[2] - b[2])
        out.setdefault("%s %d ms" % ("pitch" if key == r"\U" else "roll", ms), []).append(delta)
    s_values = sorted({r[3] for r in rows})
    return {k: dict(mean=round(statistics.mean(v)), each=v) for k, v in sorted(out.items())}, s_values, rows[-1][4] if rows else None


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--machine", choices=("machine", "machine386", "dosbox-x", "86box"), required=True,
                    help="machine386: this machine under --timing 386 (the interpreter, 33.33 M cycles a second)")
    ap.add_argument("--data", required=True)
    ap.add_argument("--out", type=Path, required=True)
    a = ap.parse_args()
    route = (HERE / "routes" / "cargo_pilot.input").read_text().splitlines()
    time_us = int(route[0].split("time_us=")[1])
    if a.machine in ("machine", "machine386"):
        a.out.mkdir(parents=True)
        replay = [l.split() for l in route[1:] if l and not l.startswith("#")]
        pos = 0
        if a.machine == "machine386":
            os.environ["F117R_TIMING"] = "386"
        ips, engine = (33_333_333, "interp") if a.machine == "machine386" else (9_000_000, "recomp")
        k = ips / 9_000_000
        with Machine(a.data, tempfile.mkdtemp(dir=a.out), engine=engine, time_us=time_us, ips=ips) as m:
            while m.program != "VGAME.EXE":
                while pos < len(replay) and int(int(replay[pos][1]) * k) < m.clock + m.ips:
                    q = replay[pos]
                    at = int(int(q[1]) * k)
                    if q[0] == "K": m.key(at, int(q[2], 16))
                    else: m.mouse(at, *map(int, q[2:5]))
                    pos += 1
                m.run_until(m.clock + int(90_000 * k))
            result = run(m, m.start)
    else:
        a.out.mkdir(parents=True)
        if a.machine == "dosbox-x":
            import dosbox_cargo_pilot as backend
            make = backend.DosboxMachine
        else:
            import b86_cargo_pilot as backend
            make = backend.B86Machine
            make.frame_taps = False          # the taps as sent: this measures what frame taps correct
        with make(a.data, route, a.out / "run", time_us, True) if a.machine == "86box" else \
                make(a.data, route, a.out / "run", time_us, True) as m:
            result = run(m, m.start)
    report = dict(machine=a.machine, response=result[0], S=result[1], agl_at_end=result[2])
    (a.out / "response.json").write_text(json.dumps(report, indent=1) + "\n")
    print(json.dumps({k: v for k, v in report.items()}, indent=1))


if __name__ == "__main__":
    raise SystemExit(main())
