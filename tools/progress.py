#!/usr/bin/env python3
"""progress.py - how much of the roadmap is done, by phase and overall.

    py tools/progress.py              # the table and the commit-title suffix
    py tools/progress.py --title      # only the suffix, e.g. (P1 85.90%, P2 6.93%, P3 15.00%, All 46.83%)
    py tools/progress.py --census FILE.tsv   # refresh the census figures from reimp_names.py output

Each phase is a weighted list of items in docs/progress.json: weight is the
item's share of the phase's effort, done its completion. Items marked
"measured" are computed here from the repository - matched routines from
src/matched/matched.c against the census, fixes from src/fixes/fixes.c
against the defects in docs/bugs.md - and the rest are estimates with their
evidence written beside them. Overall is the phases weighted by their share
of the whole effort. docs/progress.md explains the choices.
"""
from __future__ import annotations

import argparse
import json
import os
import re

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DATA = os.path.join(ROOT, "docs", "progress.json")


def matched_addresses():
    text = open(os.path.join(ROOT, "src", "matched", "matched.c"), encoding="utf-8").read()
    return {(mod, int(seg, 16) * 16 + int(ip, 16))
            for mod, seg, ip in re.findall(r'"(\w+)\.EXE", \w+, 0x([0-9A-F]{4}), 0x([0-9A-F]{4})', text)}


def fixes_share():
    fixes = open(os.path.join(ROOT, "src", "fixes", "fixes.c"), encoding="utf-8").read()
    table = fixes[fixes.index("FIXES[] = {"):]
    available = set(re.findall(r'\{\s*"(D\d+)",', table[:table.index("};")]))
    bugs = open(os.path.join(ROOT, "docs", "bugs.md"), encoding="utf-8").read()
    catalogued = set(re.findall(r"^###\s+(D\d+)\.", bugs, re.M))
    return len(available & catalogued) / max(len(catalogued), 1), len(available & catalogued), len(catalogued)


def refresh_census(data, path):
    rows = [l.rstrip("\n").split("\t") for l in open(path, encoding="utf-8")][1:]
    matched = matched_addresses()
    c = data["census"]
    c["functions"] = len(rows)
    c["bytes"] = sum(int(r[2]) for r in rows)
    hit = [r for r in rows if (r[0], int(r[1], 16)) in matched]
    c["matched_functions"] = len(hit)
    c["matched_bytes"] = sum(int(r[2]) for r in hit)


def compute(data):
    c = data["census"]
    share, fixed, catalogued = fixes_share()
    measures = {
        "census_matched_bytes": c["matched_bytes"] / c["bytes"],
        "fixes_share": share,
    }
    out = {}
    rows = []
    for phase, items in data["phases"].items():
        total = sum(i["weight"] for i in items)
        got = 0.0
        for i in items:
            done = measures[i["done"]] if isinstance(i["done"], str) else float(i["done"])
            got += i["weight"] * done
            rows.append((phase, i["item"], i["weight"], done, i["kind"]))
        out[phase] = 100.0 * got / total
    w = data["phase_weights"]
    out["All"] = sum(out[p] * w[p] for p in w) / sum(w.values())
    return out, rows, (fixed, catalogued)


def title(pct):
    return "(P1 %.2f%%, P2 %.2f%%, P3 %.2f%%, All %.2f%%)" % (pct["P1"], pct["P2"], pct["P3"], pct["All"])


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--title", action="store_true", help="print only the commit-title suffix")
    ap.add_argument("--census", help="reimp_names.py TSV: refresh the census figures in progress.json")
    a = ap.parse_args()
    data = json.load(open(DATA, encoding="utf-8"))
    if a.census:
        refresh_census(data, a.census)
        with open(DATA, "w", encoding="utf-8", newline="\n") as f:
            json.dump(data, f, indent=2)
            f.write("\n")
    pct, rows, (fixed, catalogued) = compute(data)
    if a.title:
        print(title(pct))
        return
    for phase, item, weight, done, kind in rows:
        print("%-3s %5.1f%% of weight %3d  %-9s %s" % (phase, 100 * done, weight, kind, item))
    c = data["census"]
    print("census: %d of %d functions matched, %d of %d bytes; fixes %d of %d catalogued defects"
          % (c["matched_functions"], c["functions"], c["matched_bytes"], c["bytes"], fixed, catalogued))
    print(title(pct))


if __name__ == "__main__":
    main()
