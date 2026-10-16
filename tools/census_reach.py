#!/usr/bin/env python3
"""Which unmatched census routines does any route actually run?

A routine only counts as matched once a real route executes it
(docs/roadmap.md, handoff.md), so the work left in Phase 2 is the unmatched
census code that routes reach, plus whatever new routes would reach. This
joins the census (reimp_names.py output) with coverage files (*.cov: module,
code hash, segment, offset and instruction bytes) and the matched table in
src/matched/matched.c.

The gate's recomp coverage records only INTERPRETED fallback instructions.
It proves positive reach, but an absent entry may already run as translated
code. To inventory a whole route, collect fresh --engine interp --coverage
FILE runs. Neither source proves that an absent entry is unreachable.

    py tools/census_reach.py --census census.tsv --coverage DIR [--out reached.tsv]

For example: py tools/run_route.py tools/routes/full_cycle.args --data DIR
--engine interp --out NEW_DIR -- --coverage NEW_DIR/full_cycle.cov

It prints entries with recorded execution, entries with only interior
instructions recorded, and entries without evidence in these files.
A census row with no region of ours is an entry our translation did not
start a region at (an interior entry, or data such as SETUP 0x027AE's
strings); it is judged by its linear address (segment * 16 + offset) and
counted separately, so code is not mistaken for data or data for code.
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
    linear = collections.defaultdict(dict)       # module -> {linear address: routes}
    for path in sorted(glob.glob(os.path.join(directory, "*.cov"))):
        route = os.path.basename(path)[:-4]
        for line in open(path, errors="replace"):
            parts = line.split()
            if len(parts) >= 4:
                key = (parts[0], parts[2].upper(), parts[3].upper())
                if not entries[key]:
                    inside[parts[0]].append((key[1], int(key[2], 16)))
                entries[key].add(route)
                linear[parts[0]].setdefault(int(key[1], 16) * 16 + int(key[2], 16), set()).add(route)
    return entries, inside, linear


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--census", required=True, help="reimp_names.py TSV")
    ap.add_argument("--coverage", required=True, help="directory of *.cov files; use fresh interpreter captures for complete route coverage")
    ap.add_argument("--out", help="write the entries routes enter, largest first, as TSV")
    args = ap.parse_args()

    matched = progress.matched_addresses()
    entries, inside, linear = load_coverage(args.coverage)
    reached, unseen, partial = [], [], []
    regionless = {"entered": [], "interior": [], "unseen": []}
    for line in list(open(args.census))[1:]:
        row = line.rstrip("\n").split("\t")
        binary, entry, size, region = row[0], row[1], int(row[2]), row[3]
        if (binary, int(entry, 16)) in matched:
            continue
        if not region:
            ran = linear.get(binary + ".EXE", {})
            start = int(entry, 16)
            record = (binary, entry, size, "(no region)", sorted(ran.get(start, ())))
            if start in ran:
                regionless["entered"].append(record)
            elif any(start <= a < start + max(size, 1) for a in ran):
                regionless["interior"].append(record)
            else:
                regionless["unseen"].append(record)
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
            unseen.append(record)

    def total(rows):
        return len(rows), sum(r[2] for r in rows)

    print("unmatched code entries entered by a route: %d (%d bytes)" % total(reached))
    print("interior instructions recorded, entry not recorded: %d (%d bytes)" % total(partial))
    print("no execution evidence in these files: %d (%d bytes)" % total(unseen))
    print("rows with no region of ours: entry run %d (%d bytes), interior only %d (%d bytes), "
          "no evidence %d (%d bytes, data included)" % (*total(regionless["entered"]), *total(regionless["interior"]),
                                                        *total(regionless["unseen"])))
    print("absence is not unreachability: recomp coverage omits already-translated execution")
    for label, rows in (("entered", reached), ("unseen", unseen)):
        per = collections.Counter()
        for r in rows:
            per[r[0]] += r[2]
        print("  %s, bytes by program: %s" % (label, dict(sorted(per.items()))))
    if args.out:
        with open(args.out, "w") as out:
            for r in sorted(reached + regionless["entered"], key=lambda r: -r[2]):
                out.write("\t".join([r[0], r[1], str(r[2]), r[3], str(len(r[4])), ",".join(r[4])[:80]]) + "\n")
        print("wrote", args.out)


if __name__ == "__main__":
    main()
