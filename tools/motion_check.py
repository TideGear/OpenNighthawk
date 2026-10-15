#!/usr/bin/env python3
"""motion_check.py LOG [--json FILE]: how smoothly a flight can move between logic frames.

The presentation checks prove every logic frame exact; they cannot see motion that is uneven
between frames. This reports, from an observer log of a flight (f117run --observe with
F117R_OBSERVE_PAGES=1, F117R_OBSERVE_HUD=1 for the map record 'm', and F117R_OBSERVE_IDENT=1 for
identity records 'I'):

  timing   - the aircraft's step (its 32-bit map position, 'm') against the step's emulated
             duration. The game moves a nearly fixed distance per step while steps take different
             times; in-between frames timed on the emulated clock then change apparent speed at
             each step boundary. Reported: the share of boundaries where the duration changes by
             more than 10/20/30%, and the range of apparent speed.
  pairing  - with the presentation's pairing (interp_frame.Pairing, identity first): polygons and
             outline edges with no partner in the next step (they switch at t = 0.5 instead of
             moving), and held steps (cuts).
"""
from __future__ import annotations

import argparse
import json
import sys

import interp_frame as I


def timing(frames):
    """Per-step distance and duration from each frame's first map record ('m')."""
    pts = []
    for f in frames:
        for r in f.recs:
            if r.kind == "m" and len(r.v) >= 7:
                v = r.v
                pts.append((r.icount, (v[3] & 0xFFFF) | (v[4] & 0xFFFF) << 16, (v[5] & 0xFFFF) | (v[6] & 0xFFFF) << 16))
                break
    if len(pts) < 3:
        return None
    dt = [b[0] - a[0] for a, b in zip(pts, pts[1:])]
    dist = [((b[1] - a[1]) ** 2 + (b[2] - a[2]) ** 2) ** 0.5 for a, b in zip(pts, pts[1:])]
    change = [abs(b / a - 1) for a, b in zip(dt, dt[1:]) if a > 0]
    speed = [d / t for d, t in zip(dist, dt) if t > 0 and d > 0]
    out = dict(steps=len(dt), step_clocks_min=min(dt), step_clocks_median=sorted(dt)[len(dt) // 2], step_clocks_max=max(dt))
    for th in (10, 20, 30):
        out["boundaries_over_%d_pct" % th] = round(100.0 * sum(c > th / 100 for c in change) / max(1, len(change)), 1)
    out["apparent_speed_range"] = round(max(speed) / min(speed), 2) if speed else None
    return out


def pairing(frames):
    polys = paired = lines = lines_paired = cuts = 0
    for n in range(len(frames) - 1):
        P = I.Pairing(frames[n], frames[n + 1])
        polys += len(frames[n].polys)
        paired += len(P.paired_a)
        lines += sum(len(b.lines) for b in frames[n].batches)
        lines_paired += sum(len(frames[n].batches[i].lines) for i in P.batches_a)
        cuts += 1 if I.is_cut(P) else 0
    have_ident = any(b.ident is not None for f in frames for b in f.batches)
    return dict(step_pairs=len(frames) - 1, identity_records=have_ident,
                polygons=polys, polygons_unpaired_pct=round(100.0 * (polys - paired) / max(1, polys), 2),
                outline_edges=lines, outline_edges_unpaired_pct=round(100.0 * (lines - lines_paired) / max(1, lines), 2),
                held_steps=cuts)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("log")
    ap.add_argument("--json", help="also write the result here")
    a = ap.parse_args(argv)
    frames = I.frames_from(I.read_log(a.log))
    result = dict(log=a.log, logic_frames=len(frames), timing=timing(frames), pairing=pairing(frames))
    print(json.dumps(result, indent=2))
    if a.json:
        with open(a.json, "w") as f:
            json.dump(result, f, indent=2)
    return 0


if __name__ == "__main__":
    sys.exit(main())
