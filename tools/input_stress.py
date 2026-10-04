#!/usr/bin/env python3
"""Replay overlapping input, repeats, mouse buttons and game-port extremes.

Uses the normal recorded takeoff; no guest state is modified. Equality only
compares our two engines, not physical hardware or DOSBox's input mapping.
"""
import argparse
import json
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def events():
    out = []
    def key(at, *scans):
        out.extend(f"K {at} {scan:02X}" for scan in scans)
    # Both arrows held together, with proper extended make/break streams.
    key(3100000000, 0xE0, 0x50, 0xE0, 0x4B)
    key(3100540000, 0xE0, 0xD0, 0xE0, 0xCB)
    # Control, Shift and a letter; rapid typematic makes then one release.
    key(3120000000, 0x1D, 0x2A, 0x1E)
    for i in range(16): key(3120090000 + i * 90000, 0x1E)
    key(3122000000, 0x9E, 0xAA, 0x9D)
    # Right modifiers, Print Screen and Pause's multi-byte sequences.
    key(3140000000, 0xE0, 0x1D, 0xE0, 0x38)
    key(3140540000, 0xE0, 0x9D, 0xE0, 0xB8)
    key(3150000000, 0xE0, 0x2A, 0xE0, 0x37)
    key(3150540000, 0xE0, 0xB7, 0xE0, 0xAA)
    key(3160000000, 0xE1, 0x1D, 0x45, 0xE1, 0x9D, 0xC5)
    # Keyboard delivery immediately either side of scanout/PIT boundaries.
    for i in range(40):
        boundary = ((3180000000 + i * 128413) // 128413) * 128413
        key(boundary - 1, 0x30); key(boundary + 1, 0xB0)
    for i, (x, y, buttons) in enumerate([(0, 0, 0), (319, 199, 3), (160, 100, 2), (160, 100, 0)]):
        out.append(f"M {3200000000 + i * 900000} {x} {y} {buttons} {i - 2} {2 - i}")
    for i, (x, y, buttons) in enumerate([(128, 128, 0), (0, 0, 3), (255, 255, 0), (0, 255, 1), (255, 0, 2), (128, 128, 0)]):
        out.append(f"J {3250000000 + i * 900000} 1 {x} {y} 256 256 {buttons}")
    out.append("J 3260000000 0 256 256 256 256 0")
    return sorted(out, key=lambda line: int(line.split()[1]))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--data", required=True)
    parser.add_argument("--out", required=True)
    args = parser.parse_args()
    out = Path(args.out); out.mkdir(parents=True, exist_ok=True)
    lines = (ROOT / "tools/routes/landing.input").read_text().splitlines()
    prefix = [line for line in lines[1:] if line and not line.startswith("#") and int(line.split()[1]) < 3000000000]
    replay = out / "stress.input"
    replay.write_text(lines[0] + "\n" + "\n".join(prefix + events()) + "\n")
    reports = []
    for engine in ("interp", "recomp"):
        folder = out / engine; folder.mkdir(exist_ok=True)
        proc = subprocess.run([str(ROOT / "build/f117run.exe"), "--engine", engine,
            "--data", args.data, "--save", str(folder / "save"), "--log", str(folder / "run.log"),
            "--replay", str(replay), "--record", str(folder / "applied.input"),
            "--steps", "3600000000", "--hash-every", "25000000"], capture_output=True, text=True)
        (folder / "runner.txt").write_text(proc.stdout + proc.stderr)
        proc.check_returncode()
        final = re.search(r"stopped at icount (\d+).*final hash ([0-9a-f]+)", proc.stdout)
        hashes = re.findall(r"^\[hash\] (\d+) ([0-9a-f]+) (\S+)", proc.stdout, re.M)
        applied = (folder / "applied.input").read_text().splitlines()[1:]
        reports.append({"engine": engine, "final": final.groups() if final else None,
            "hashes": hashes, "all_inputs_applied": applied == prefix + events(),
            "flight_checkpoints": sum(row[2] == "VGAME.EXE" for row in hashes)})
    errors = []
    if reports[0]["final"] != reports[1]["final"] or reports[0]["hashes"] != reports[1]["hashes"]:
        errors.append("engine states differ")
    if not all(r["all_inputs_applied"] and r["flight_checkpoints"] for r in reports):
        errors.append("not all inputs reached a flight")
    result = {"errors": errors, "inputs": len(prefix) + len(events()), "stress_inputs": len(events()), "runs": reports}
    (out / "result.json").write_text(json.dumps(result, indent=2) + "\n")
    print({"errors": errors, "inputs": result["inputs"], "final": reports[0]["final"],
           "checkpoints": len(reports[0]["hashes"]), "flight_checkpoints": reports[0]["flight_checkpoints"]}, flush=True)
    return int(bool(errors))


if __name__ == "__main__":
    raise SystemExit(main())
