#!/usr/bin/env python3
"""Judge an SDL --present-log capture at an explicitly selected display target.

This checks submitted pictures and reported display settings. It cannot
verify a physical panel, tearing or the perceived quality of motion.
"""
import argparse
import json
import math
from pathlib import Path
import re


def percentile(values, fraction):
    ordered = sorted(values)
    return ordered[min(len(ordered) - 1, math.ceil(len(ordered) * fraction) - 1)]


def check(text, width=3840, height=2160, refresh=60.0, ips=9000000,
          min_frames=600, max_missed_percent=1.0):
    if min(width, height, ips, min_frames) <= 0 or not math.isfinite(refresh) or refresh <= 0:
        raise ValueError("resolution, refresh, ips and minimum frames must be positive")
    errors = []
    settings = set(re.findall(r"^# display renderer=(\S+) output=(\d+)x(\d+) refresh=([\d.]+) vsync=(-?\d+) scale=(\d+)$", text, re.M))
    if len(settings) != 1:
        raise ValueError("missing or ambiguous display metadata")
    backend, w, h, hz, vsync, scale = next(iter(settings))
    if backend in ("software", "dummy"):
        errors.append("GPU renderer required")
    if (int(w), int(h)) != (width, height):
        errors.append("render output resolution differs from target")
    if abs(float(hz) - refresh) > 0.5:
        errors.append("reported refresh differs from target")
    if int(vsync) != 1:
        errors.append("vsync is not enabled")
    if int(scale) < math.ceil(min(width, height * 4 / 3) / 320):
        errors.append("fine rendering scale is below the 4:3 viewport width")
    rows = []
    for line in text.splitlines():
        if not line or line.startswith(("#", "host_ns ")):
            continue
        fields = line.split()
        if len(fields) != 6:
            raise ValueError("malformed presentation row")
        host, clock, older, newer = map(int, fields[:4])
        t, replayed = float(fields[4]), int(fields[5])
        if not math.isfinite(t) or replayed not in (0, 1):
            raise ValueError("invalid interpolation time or replay flag")
        if rows and (host <= rows[-1][0] or clock < rows[-1][1]):
            raise ValueError("host or guest clock moved backwards")
        rows.append((host, clock, older, newer, t, replayed))
    flight = [row for row in rows if row[5] and row[2] and row[3]]
    if len(flight) < min_frames:
        raise ValueError("too few interpolated flight frames")
    intervals = [(b[0] - a[0]) / 1e6 for a, b in zip(rows, rows[1:])
                 if a[5] and b[5] and a[2] and b[2] and a[3] and b[3]]
    if not intervals:
        raise ValueError("no consecutive flight frames")
    period = 1000.0 / refresh
    median, p99 = percentile(intervals, .5), percentile(intervals, .99)
    missed = sum(value > 1.5 * period for value in intervals)
    missed_percent = 100.0 * missed / len(intervals)
    if abs(median - period) > .1 * period:
        errors.append("median interval differs from target refresh by more than 10%")
    if p99 > 1.25 * period:
        errors.append("p99 interval exceeds 125% of the refresh period")
    if missed_percent > max_missed_percent:
        errors.append("too many intervals exceed 150% of the refresh period")
    drift = ((flight[-1][0] - flight[0][0]) / 1e9 - (flight[-1][1] - flight[0][1]) / ips)
    if abs(drift) > .1:
        errors.append("guest clock fell more than 100 ms behind the host")
    advances = sum(b[4] > a[4] for a, b in zip(flight, flight[1:]) if a[2:4] == b[2:4])
    backwards = sum(b[4] < a[4] for a, b in zip(flight, flight[1:]) if a[2:4] == b[2:4])
    if advances < 1 or backwards:
        errors.append("interpolation time did not advance consistently within frame pairs")
    return dict(renderer=backend, output=[int(w), int(h)], refresh=float(hz),
                scale=int(scale), flight_frames=len(flight), interval_ms=dict(
                    median=median, p99=p99, maximum=max(intervals)),
                missed_intervals=missed, missed_percent=missed_percent,
                guest_drift_seconds=drift, interpolation_advances=advances,
                errors=errors)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", type=Path)
    parser.add_argument("--out", required=True, type=Path)
    parser.add_argument("--resolution", nargs=2, type=int, default=[3840, 2160])
    parser.add_argument("--refresh", type=float, default=60.0)
    parser.add_argument("--ips", type=int, default=9000000)
    args = parser.parse_args()
    if min(*args.resolution, args.ips) <= 0 or not math.isfinite(args.refresh) or args.refresh <= 0:
        parser.error("resolution, refresh and ips must be positive")
    result = check(args.log.read_text(), *args.resolution, args.refresh, args.ips)
    args.out.write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(result, indent=2))
    return int(bool(result["errors"]))


if __name__ == "__main__":
    raise SystemExit(main())
