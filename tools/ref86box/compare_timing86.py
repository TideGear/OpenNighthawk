#!/usr/bin/env python3
"""Compare held pictures from an actual 386 capture with 86Box's trace.

    py tools/ref86box/compare_timing86.py OURS_386_RUN 86BOX_frames.csv [--json REPORT]

OURS is frames386.py's ours directory (settings, frame log and changed PPMs).
Its command must select --timing 386. Pair held pictures by content, never
by duration. Missing reference scanouts are screen-off gaps, not hold time.
"""
import argparse
import csv
import hashlib
import json
from pathlib import Path

import numpy as np
from PIL import Image

CLOCK = 33_333_333
FRAME_SECONDS = 359200 / 25175000
# Tightened 10 Oct 2026 from the original 0.35/2.2 (roughly 1.6x/2.0x the
# measured 0.214/1.113), which left room for the measured value to nearly
# double before the check would notice. ~1.4x/1.35x margin, from a single
# measured run (no repeat-capture variance data yet); loosen again if a
# clean run fails here for a reason unrelated to the engine.
LIMITS = dict(min_paired=17, max_duration_diff=0.30, max_drift=1.5)


def picture_hash(path):
    """86box-trace.patch's FNV64 over doubled BGRX pixels (X=FF).

    Recover the original six DAC bits, expand as 86Box does, and reconstruct
    two equal pixels per uint64 and two scanlines per source row.
    """
    with Image.open(path) as image:
        if image.size != (320, 200):
            raise ValueError(f"not a mode-13h picture: {path}")
        rgb = np.asarray(image.convert("RGB"), dtype=np.uint32) >> 2
    rgb = rgb * 255 // 63
    pixels = (rgb[:, :, 0] << 16) | (rgb[:, :, 1] << 8) | rgb[:, :, 2] | np.uint32(0xFF000000)
    value = 1469598103934665603
    for row in pixels:
        for _ in range(2):
            for pixel in row:
                p = int(pixel)
                value = ((value ^ (p | (p << 32))) * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return f"{value:016x}"


def ours_scenes(run):
    run = Path(run)
    if not run.is_dir():
        raise ValueError("a frames386.py capture directory is required; DOSBox comparison.json is a different profile")
    command = json.loads((run / "settings.json").read_text(encoding="utf-8")).get("command", [])
    if "--timing" not in command or command[command.index("--timing") + 1:][:1] != ["386"]:
        raise ValueError("capture did not record --timing 386")
    if "--ips" in command and command[command.index("--ips") + 1:][:1] != [str(CLOCK)]:
        raise ValueError("capture uses a different 386 clock")
    keys = [line.split() for line in (run / "input.log").read_text(encoding="utf-8").splitlines()]
    key = next((int(p[1]) / CLOCK for p in keys if len(p) == 3 and p[0] == "K" and p[2] == "03"), None)
    if key is None:
        raise ValueError("SETUP's final key is missing from the capture")
    changes, current, token = [], None, None
    with (run / "frames.csv").open(encoding="utf-8", newline="") as source:
        for row in csv.DictReader(source):
            t = int(row["frame_icount"]) / CLOCK
            if row["written"] == "1" and row["video_mode"] == "19":
                path = run / "shots" / ("shot_%011d.ppm" % int(row["icount"]))
                current = (hashlib.sha256(path.read_bytes()).hexdigest(), str(path))
            new = ("blank", None) if row["blank"] == "1" else current if row["video_mode"] == "19" else ("text", None)
            if t >= key and (new[0] if new else None) != token:
                changes.append(dict(time=t, image=new[1] if new else None))
                token = new[0] if new else None
    return [dict(time=a["time"], duration=b["time"] - a["time"], image=a["image"], hash=picture_hash(a["image"]))
            for a, b in zip(changes, changes[1:]) if a["image"] and b["time"] - a["time"] >= 1]


def box_scenes(path):
    groups, previous = [], None
    with open(path, encoding="utf-8", newline="") as source:
        for row in csv.reader(source):
            t = int(row[2]) / 1_000_000
            digest = row[5] if row[3:5] == ["640", "400"] else "text"
            if groups and t - groups[-1]["last"] > 1.5 * FRAME_SECONDS:
                groups[-1]["end"] = groups[-1]["last"] + FRAME_SECONDS
                previous = None
            if digest != previous:
                if groups and "end" not in groups[-1]:
                    groups[-1]["end"] = t
                groups.append(dict(time=t, last=t, hash=digest))
                previous = digest
            else:
                groups[-1]["last"] = t
    # The final open picture is censored by the capture's stop.
    return [dict(time=g["time"], duration=g["end"] - g["time"], hash=g["hash"])
            for g in groups if "end" in g and g["end"] - g["time"] >= 1 and g["hash"] != "text"]


def pair(ours, box):
    pairs, next_box = [], 0
    for scene in ours:
        match = next((i for i in range(next_box, len(box)) if box[i]["hash"] == scene["hash"]), None)
        if match is not None:
            pairs.append((scene, box[match]))
            next_box = match + 1
    return pairs


def judge(ours, box):
    pairs = pair(ours, box)
    duration = drift = None
    if pairs:
        offset = pairs[0][1]["time"] - pairs[0][0]["time"]
        duration = max(abs(o["duration"] - b["duration"]) for o, b in pairs)
        drift = max(abs((b["time"] - o["time"]) - offset) for o, b in pairs)
    ok = (len(pairs) == len(ours) and len(pairs) >= LIMITS["min_paired"]
          and duration <= LIMITS["max_duration_diff"] and drift <= LIMITS["max_drift"])
    return dict(pass_=ok, paired=len(pairs), ours=len(ours), reference=len(box),
                max_duration_diff=duration, max_start_drift=drift, pairs=pairs)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("ours")
    parser.add_argument("box")
    parser.add_argument("--json", dest="report")
    args = parser.parse_args()
    try:
        result = judge(ours_scenes(args.ours), box_scenes(args.box))
    except (OSError, ValueError, KeyError) as error:
        print(f"timing 86box 386 FAIL  {error}")
        return 1
    if args.report:
        Path(args.report).write_text(json.dumps(result, indent=1), encoding="utf-8")
    duration = "%.3f" % result["max_duration_diff"] if result["paired"] else "unknown"
    drift = "%.3f" % result["max_start_drift"] if result["paired"] else "unknown"
    print("timing 86box 386 %s  %d of %d held pictures paired by content (ours %d, need %d); "
          "duration difference %s s (max %.2f), start drift %s s (max %.1f)" % (
              "PASS" if result["pass_"] else "FAIL", result["paired"], result["reference"], result["ours"],
              LIMITS["min_paired"], duration, LIMITS["max_duration_diff"], drift, LIMITS["max_drift"]))
    return 0 if result["pass_"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
