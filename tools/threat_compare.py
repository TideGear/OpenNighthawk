#!/usr/bin/env python3
"""Compare current threat-profile cohorts, pairing only identical mission identities.

    py tools/threat_compare.py OUT/runs.jsonl [OTHER/runs.jsonl ...]
                              [--shift-ms 16=110 20=220]

Clock shifts identify a proposed seed family; matching mission fields are
still required. Early exits remain in the analysis. Damage is the increase
in damage selections since the first observation, not missile impact count.
Intervals bootstrap paired flight differences, 5000 draws with seed 12345.
"""
import argparse
import collections
import itertools
import json
import random
import statistics


def arm(row):
    return row["speed"] + ("+" + "+".join(sorted(row.get("fixes", []))) if row.get("fixes") else "")


def paired(rows, left, right, shifts):
    groups = collections.defaultdict(dict)
    for row in rows:
        if "error" not in row:
            key = (row["route"], row["offset_ms"] - shifts.get(row["speed"], 0))
            groups[key][arm(row)] = row
    return [(g[left], g[right]) for g in groups.values()
            if left in g and right in g and g[left].get("mission")
            and g[left]["mission"] == g[right].get("mission")]


def interval(differences):
    rng = random.Random(12345)
    estimates = sorted(statistics.mean(rng.choices(differences, k=len(differences)))
                       for _ in range(5000))
    return estimates[125], estimates[4875]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("files", nargs="+")
    parser.add_argument("--shift-ms", nargs="*", default=[])
    args = parser.parse_args()
    shifts = {speed: int(ms) for speed, ms in (s.split("=", 1) for s in args.shift_ms)}
    latest = {}
    for path in args.files:
        with open(path) as source:
            for line in source:
                row = json.loads(line)
                latest[row["tag"]] = row
    rows = list(latest.values())
    if any(r.get("telemetry_version", 0) < 5 for r in rows if "error" not in r):
        parser.error("rerun with the corrected pilot/observer (telemetry version >= 5)")
    by_arm = collections.defaultdict(list)
    for row in rows:
        if "error" in row:
            print("ERROR", row["tag"], row["error"])
            continue
        if row.get("damage_initial") is None or row.get("damage_hits") is None:
            parser.error("damage endpoints missing; rerun with current threat_profile.py")
        row["damage_delta"] = row["damage_hits"] - row["damage_initial"]
        by_arm[arm(row)].append(row)
    metrics = ("enemy_launches", "enemy_bursts", "damage_delta")
    print("arm n early launches/flight bursts/flight damage-selections/flight orbit-seconds")
    for name, cohort in sorted(by_arm.items()):
        print(name, len(cohort), sum(bool(r.get("exited")) for r in cohort),
              *(round(statistics.mean(r[k] for r in cohort), 4) for k in metrics),
              round(sum(r["orbit_seconds"] for r in cohort), 1))
    for left, right in itertools.combinations(sorted(by_arm), 2):
        pairs = paired(rows, left, right, shifts)
        print(f"{left} minus {right}: {len(pairs)} matching mission pairs")
        for metric in metrics if pairs else ():
            differences = [a[metric] - b[metric] for a, b in pairs]
            lo, hi = interval(differences)
            print(f"  {metric}: {statistics.mean(differences):+.4f} "
                  f"(95% paired bootstrap {lo:+.4f} to {hi:+.4f})")


if __name__ == "__main__":
    main()
