#!/usr/bin/env python3
"""stick_response.py: how much one stick tap moves the aircraft, on this machine, DOSBox-X or 86Box.

    py tools/stick_response.py --machine machine|dosbox-x|86box --data GOG_DIR --out DIR
                              [--ips 16000000] [--fix D1] [--time-us N]

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
from machine_api import Machine, RouteInputs  # noqa: E402
from cargo_pilot import pilot_state  # noqa: E402

TAPS = [(key, ms) for ms in (20, 60, 120, 200, 400) for key in (r"\U", r"\L")] * 3


def run(machine, start, hashes=None):
    k = machine.ips / 9_000_000          # the clock constants below are the 9 MHz model's: scaled to seconds
    machine.type(start + int(100_000_000 * k), "+")
    machine.type(start + int(170_000_000 * k), r"\D", hold_ms=1000)
    first = start + int(400_000_000 * k)
    plan = [(first + int(i * 27_000_000 * k), key, ms) for i, (key, ms) in enumerate(TAPS)]
    rows, mission = [], None
    stopped = None
    while machine.clock < plan[-1][0] + int(40_000_000 * k):
        if machine.program.upper() != "VGAME.EXE":
            stopped = "left VGAME for " + machine.program
            break
        if machine.clock - start > int(190_000_000 * k):
            s = pilot_state(machine)
            if mission is None and 1 <= s["objective_type"] <= 8:
                mission = {name: s[name] for name in ("objective_type", "target", "target_x", "target_y",
                                                     "secondary_type", "secondary_target", "departure", "home")}
            rows.append((machine.clock, s["pitch"], s["roll"], s["S"], s["agl"]))
            if hashes is not None:
                hashes.append((machine.clock, "%016x" % machine.hash))
        for at, key, ms in plan:
            if machine.clock <= at < machine.clock + machine.ips // 5:
                machine.type(at, key, hold_ms=ms)
        if machine.run_until(machine.clock + machine.ips // 5) != Machine.SLICE:
            stopped = "machine stopped in " + machine.program
            break
    out = {}
    for at, key, ms in plan:
        before = [r for r in rows if r[0] <= at]
        after = [r for r in rows if r[0] > at]
        if not before or len(after) < 4:
            continue
        b, a = before[-1], after[3]
        delta = (a[1] - b[1]) if key == r"\U" else (a[2] - b[2])
        delta = ((delta + 0x8000) & 0xFFFF) - 0x8000  # angles wrap after one turn
        out.setdefault("%s %d ms" % ("pitch" if key == r"\U" else "roll", ms), []).append(delta)
    # The executable's load notification precedes its flight initialisation.
    # Measure frame rate during the tap window, after takeoff and loading.
    s_values = sorted({r[3] for r in rows if r[0] >= first})
    response = {k: dict(mean=round(statistics.mean(v)), each=v) for k, v in sorted(out.items())}
    detail = dict(mission=mission, stopped=stopped, taps_measured=sum(len(v) for v in out.values()),
                  taps_planned=len(plan), samples=rows)
    return response, s_values, rows[-1][4] if rows else None, detail


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--machine", choices=("machine", "machine386", "dosbox-x", "86box"), required=True,
                    help="machine386: this machine under --timing 386 (33.33 M cycles a second)")
    ap.add_argument("--engine", choices=("interp", "recomp"), help="engine for machine or machine386")
    ap.add_argument("--data", required=True)
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument("--ips", type=int, help="instructions per second for --machine machine (default 9000000)")
    ap.add_argument("--fix", action="append", default=[], help="fix for --machine machine, e.g. D1")
    ap.add_argument("--time-us", type=int, help="boot clock override, for a controlled mission seed")
    ap.add_argument("--front-route", type=Path,
                    help="typed .args front end with program-relative times, instead of cargo_pilot.input")
    a = ap.parse_args()
    if a.ips is not None and (a.machine != "machine" or a.ips <= 0):
        ap.error("--ips requires --machine machine and a positive speed")
    if a.fix and a.machine != "machine":
        ap.error("--fix is supported only with --machine machine; fixes do not yet charge 386 cycles")
    if a.front_route and a.machine not in ("machine", "machine386"):
        ap.error("--front-route is supported only with the local machine")
    hashes = [] if a.machine in ("machine", "machine386") else None
    start_seed = None
    route = (HERE / "routes" / "cargo_pilot.input").read_text().splitlines()
    time_us = a.time_us if a.time_us is not None else int(route[0].split("time_us=")[1])
    if a.machine in ("machine", "machine386"):
        a.out.mkdir(parents=True)
        replay = [l.split() for l in route[1:] if l and not l.startswith("#")]
        pos = 0
        if a.machine == "machine386":
            os.environ["F117R_TIMING"] = "386"
        else:
            os.environ.pop("F117R_TIMING", None)
        ips, engine = (33_333_333, "interp") if a.machine == "machine386" else (9_000_000, "recomp")
        ips = a.ips or ips
        engine = a.engine or engine
        k = ips / 9_000_000
        front = None
        if a.front_route:
            front_args = [line.strip() for line in a.front_route.read_text().splitlines()
                          if line.strip() and not line.lstrip().startswith("#")]
            if a.time_us is None and "--time-us" in front_args:
                time_us = int(front_args[front_args.index("--time-us") + 1])
            front = RouteInputs(front_args)
            front.pending = [(p, int(at * k), option, content) for p, at, option, content in front.pending
                             if p != "VGAME.EXE"]
        with Machine(a.data, tempfile.mkdtemp(dir=a.out), engine=engine, time_us=time_us, ips=ips,
                     fixes=tuple(a.fix)) as m:
            saw_seed_init = False
            while m.program != "VGAME.EXE":
                if m.clock > 600 * ips:
                    raise RuntimeError("front end did not reach flight within ten guest minutes")
                if front is not None:
                    front.poll(m)
                if m.program == "START.EXE" and start_seed is None:
                    seed = m.read32(((m.psp + 0x10 + 0x0A95) << 4) + 0xAE8C)
                    if seed == 1:
                        saw_seed_init = True
                    elif saw_seed_init and seed < 0x10000:
                        start_seed = seed  # srand receives the BIOS tick's low word
                while front is None and pos < len(replay) and int(int(replay[pos][1]) * k) < m.clock + m.ips:
                    q = replay[pos]
                    at = int(int(q[1]) * k)
                    if q[0] == "K": m.key(at, int(q[2], 16))
                    else: m.mouse(at, *map(int, q[2:5]))
                    pos += 1
                # srand's initial word can be consumed by the first rand()
                # within a 10 ms slice. Read more often during START startup.
                seed_window = m.program == "START.EXE" and start_seed is None and m.clock - m.start < 2 * ips
                rc = m.run_until(m.clock + (1000 if seed_window else int(90_000 * k)))
                if rc != Machine.SLICE:
                    raise RuntimeError("front end stopped (%d) in %s" % (rc, m.program))
            result = run(m, m.start, hashes)
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
    report = dict(machine=a.machine, response=result[0], S=result[1], agl_at_end=result[2],
                  time_us=time_us, fixes=a.fix, front_route=str(a.front_route) if a.front_route else None,
                  start_seed=start_seed, **result[3])
    if hashes is not None:
        report.update(engine=engine, ips=ips, checkpoints=hashes)
    (a.out / "response.json").write_text(json.dumps(report, indent=1) + "\n")
    print(json.dumps({k: v for k, v in report.items()}, indent=1))


if __name__ == "__main__":
    raise SystemExit(main())
