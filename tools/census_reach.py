#!/usr/bin/env python3
"""Which unmatched census routines does any route actually run?

A routine only counts as matched once a real route executes it
(docs/roadmap.md, handoff.md), so the work left in Phase 2 is the unmatched
census code that routes reach, plus whatever new routes would reach. This
joins the census (reimp_names.py output) with the coverage files a gate
leaves (the *.cov files: module, code hash, segment, offset and bytes of every
instruction a route ran) and the matched table in src/matched/matched.c.

    py tools/census_reach.py --census census.tsv --coverage DIR [--out reached.tsv]

It prints, per program, how many unmatched code entries and bytes the routes
enter and how many they never do (a routine none enters may still be run
partway, which is counted separately). Census rows with no region are data.
"""
import argparse
import collections
import glob
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import progress  # noqa: E402


def load_coverage(directory):
    entries = collections.defaultdict(set)       # (module, segment, offset) -> routes that ran it
    inside = collections.defaultdict(list)       # module -> [(segment, offset)]
    for path in sorted(glob.glob(os.path.join(directory, "*.cov"))):
        route = os.path.basename(path)[:-4]
        for line in open(path, errors="replace"):
            parts = line.split()
            if len(parts) >= 4:
                key = (parts[0], parts[2].upper(), parts[3].upper())
                if not entries[key]:
                    inside[parts[0]].append((key[1], int(key[2], 16)))
                entries[key].add(route)
    return entries, inside


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--census", required=True, help="reimp_names.py TSV")
    ap.add_argument("--coverage", required=True, help="directory of the gate's *.cov files")
    ap.add_argument("--out", help="write the entries routes enter, largest first, as TSV")
    args = ap.parse_args()

    matched = progress.matched_addresses()
    entries, inside = load_coverage(args.coverage)
    reached, never, partial = [], [], []
    for line in list(open(args.census))[1:]:
        row = line.rstrip("\n").split("\t")
        binary, entry, size, region = row[0], row[1], int(row[2]), row[3]
        if (binary, int(entry, 16)) in matched or not region:
            continue
        m = re.match(r"R_([A-Z_0-9]+?)_EXE_([0-9A-F]{4})_([0-9A-F]{4})$", region)
        if not m:
            continue
        module, seg, ip = m.group(1) + ".EXE", m.group(2), int(m.group(3), 16)
        key = (module, seg, "%04X" % ip)
        record = (binary, entry, size, region, sorted(entries.get(key, ())))
        if entries.get(key):
            reached.append(record)
        elif any(s == seg and ip <= a < ip + max(size, 1) for s, a in inside[module]):
            partial.append(record)
        else:
            never.append(record)

    def total(rows):
        return len(rows), sum(r[2] for r in rows)

    print("unmatched code entries entered by a route: %d (%d bytes)" % total(reached))
    print("entered only partway (an instruction inside ran, the entry did not): %d (%d bytes)" % total(partial))
    print("never run by any route: %d (%d bytes)" % total(never))
    for label, rows in (("entered", reached), ("never", never)):
        per = collections.Counter()
        for r in rows:
            per[r[0]] += r[2]
        print("  %s, bytes by program: %s" % (label, dict(sorted(per.items()))))
    if args.out:
        with open(args.out, "w") as out:
            for r in sorted(reached, key=lambda r: -r[2]):
                out.write("\t".join([r[0], r[1], str(r[2]), r[3], str(len(r[4])), ",".join(r[4])[:80]]) + "\n")
        print("wrote", args.out)


if __name__ == "__main__":
    main()
