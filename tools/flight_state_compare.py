#!/usr/bin/env python3
"""Compare a read-only DOSBox flight capture with our normal-input flight.

Wall-time window input and instruction-clock replay are not exactly aligned.
The report separates initialized mission state from flight trajectories and
never treats approximate timing as an exact whole-machine parity pass.
"""
import argparse
import csv
import json
from pathlib import Path
import tempfile

from machine_api import Machine, RouteInputs
from random_flights import base_route
from landing_pilot import observe


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--data", required=True)
    parser.add_argument("--reference", required=True)
    parser.add_argument("--out", required=True)
    parser.add_argument("--engine", choices=("interp", "recomp"), default="recomp")
    parser.add_argument("--skip-intro", action="store_true", help="match a reference captured with --skip-intro")
    args = parser.parse_args()
    reference = [{key: float(value) for key, value in row.items()}
                 for row in csv.DictReader(Path(args.reference).open())]
    # Exclude VGAME's partially initialized loading state.
    reference = [row for row in reference if row["fuel"] == 10000 or row["x"] != 0 and row["frame"] > 0]
    if not reference: raise ValueError("no initialized reference flight")
    out = Path(args.out); out.mkdir(parents=True, exist_ok=True)
    inputs = RouteInputs(base_route() + (["--type", r"PLAYER.EXE+9000000:\s"] if args.skip_intro else []) + [
        "--type", "VGAME.EXE+99990000:+", "--type", r"VGAME.EXE+170010000:~1000:\D",
        "--type", "VGAME.EXE+270000000:6", "--type", r"VGAME.EXE+360000000:\2",
        "--type", "VGAME.EXE+450000000:8", "--type", r"VGAME.EXE+495000000:\s",
        "--type", r"VGAME.EXE+540000000:\r"])
    rows = []
    with Machine(args.data, tempfile.mkdtemp(dir=out), log=out / "run.log", engine=args.engine) as machine:
        machine.record(out / "input.log")
        while machine.clock < 5000000000:
            inputs.poll(machine)
            if machine.program == "VGAME.EXE":
                elapsed = (machine.clock - machine.start) / machine.ips
                if elapsed > 2:
                    state = observe(machine)
                    ds = (machine.psp + 0x10 + 0x1E42) << 4
                    offsets = {"primary_type": 0xE304, "primary_target": 0xE306,
                        "secondary_type": 0xE316, "events": 0x951A, "frame": 0x3D8E,
                        "rng": 0x929E, "mission_time": 0x9912}
                    state.update({key: machine.read16(ds + offset) for key, offset in offsets.items()})
                    rows.append({"seconds": elapsed, **state})
                    if elapsed > 90: break
            if machine.run_until(machine.clock + 90000) != Machine.SLICE: break
        machine.screen(out / "final.ppm")
    if not rows: raise RuntimeError("our run never reached flight")
    with (out / "flight.csv").open("w", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=rows[0].keys()); writer.writeheader(); writer.writerows(rows)
    def nearest(source, seconds): return min(source, key=lambda row: abs(row["seconds"] - seconds))
    static = ("heading", "pitch", "roll", "altitude", "throttle", "speed", "x", "y", "fuel", "home", "departure",
              "primary_type", "primary_target", "secondary_type", "home_x", "home_y")
    r, o = nearest(reference, 5), nearest(rows, 5)
    initial = {key: {"reference": r[key], "ours": o[key]} for key in static if r[key] != o[key]}
    checkpoints = []
    for when in (5, 10, 20, 30, 60, 90):
        r, o = nearest(reference, when), nearest(rows, when)
        fields = ("heading", "pitch", "roll", "altitude", "speed", "x", "y", "fuel", "flags", "S", "frame", "rng", "mission_time")
        checkpoints.append({"requested_seconds": when, "reference_seconds": r["seconds"], "ours_seconds": o["seconds"],
                            "fields": {key: {"reference": r[key], "ours": o[key], "difference": o[key] - r[key]} for key in fields}})
    trajectory_differs = any(any(values["difference"] != 0 for values in point["fields"].values()) for point in checkpoints)
    result = {"initial_mission_fields_compared": len(static), "initial_mission_differences": initial,
        "observed_fields_differ": trajectory_differs,
        "checkpoints": checkpoints, "exact_clock_aligned": False,
        "limitations": "DOSBox input via Windows messages at wall-time deadlines (~100 ms polling); reference not frozen while reading RAM; trajectories are diagnostics, not exact parity verdicts"}
    (out / "comparison.json").write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps({"initial_mission_differences": initial, "final_checkpoint": checkpoints[-1]}, indent=2), flush=True)
    return int(bool(initial) or trajectory_differs)


if __name__ == "__main__":
    raise SystemExit(main())
