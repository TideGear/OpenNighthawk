"""Validate and summarize F117_GPU_TIMESTAMP_LOG CSV (hardware GPU intervals).

Usage: py tools/renderer_bench/timestamp_report.py path/to/passes.csv
Intervals crossing submissions can include idle time. They are not frame costs.
"""
import argparse
import csv
import math
from collections import defaultdict
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("csv", type=Path)
    args = parser.parse_args()
    intervals = defaultdict(list)
    frames = 0
    previous = None
    expected = ["upload_begin", "upload_end", "bands_end", "polygons_end",
                "points_end", "strokes_end", "picture_upload_begin",
                "picture_upload_end", "replace_end", "present_end", "download_end"]
    frame_labels = []
    with args.csv.open(newline="", encoding="utf-8") as stream:
        reader = csv.DictReader(stream)
        if reader.fieldnames != ["from", "to", "gpu_ms"]:
            raise SystemExit("Invalid timestamp header; no timings accepted")
        for line, row in enumerate(reader, 2):
            try:
                value = float(row["gpu_ms"])
            except (ValueError, TypeError):
                raise SystemExit(f"Invalid GPU sample at line {line}") from None
            if not math.isfinite(value) or value < 0 or not row["from"] or not row["to"]:
                raise SystemExit(f"Invalid GPU sample at line {line}")
            if row["from"] == "upload_begin":
                if previous not in (None, "download_end"):
                    raise SystemExit(f"Incomplete previous frame at line {line}")
                frames += 1
                frame_labels = [row["from"]]
            elif previous != row["from"]:
                raise SystemExit(f"Broken interval sequence at line {line}")
            previous = row["to"]
            frame_labels.append(previous)
            if previous == "download_end":
                allowed = expected if "points_end" in frame_labels else [x for x in expected if x != "points_end"]
                if frame_labels != allowed:
                    raise SystemExit(f"Unexpected pass boundaries at line {line}")
            intervals[(row["from"], row["to"])].append(value)
    if not frames or previous != "download_end":
        raise SystemExit("No complete timestamp frame; capture may still be running")
    print(f"{frames} frames; GPU intervals in ms (not CPU or whole-frame cost)")
    for (start, end), values in intervals.items():
        values.sort()
        p95 = values[min(len(values) - 1, int(len(values) * .95))]
        print(f"{start} -> {end}: n={len(values)} mean={sum(values)/len(values):.3f} p95={p95:.3f}")


if __name__ == "__main__":
    main()
