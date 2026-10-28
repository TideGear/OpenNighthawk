#!/usr/bin/env python3
"""progress.py - how much of the roadmap is done, by phase and overall.

    py tools/progress.py              # the table and the commit-title suffix
    py tools/progress.py --title      # only the suffix, e.g. (P1 100.00%, P2 67.37%, P3 82.75%, All 85.68%)
    py tools/progress.py --census FILE.tsv --named-cover DIR --route-cover DIR --data GOG_DIR
                                      # refresh the census figures: reimp_names.py output, the named
                                      # coverage func_lockstep writes with F117_NAMED_COVER, and
                                      # interpreter --coverage captures of every route; it also
                                      # refreshes the gameplay score (census.gameplay) from the
                                      # same byte sets and docs/reference-reviewed.tsv

Each phase is a weighted list of items in docs/progress.json: weight is the
item's share of the phase's effort, done its completion. Items marked
"measured" are computed here from the repository - matched routines from
src/matched/matched.c against the bytes the routes execute (the gameplay
score) and against the all-code census (the previous measure, which still
scores the naming item), fixes from src/fixes/fixes.c
against the defects in docs/bugs.md - and the rest are estimates with their
evidence written beside them. Overall is the phases weighted by their share
of the whole effort. docs/progress.md explains the choices.
"""
from __future__ import annotations

import argparse
import glob
import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DATA = os.path.join(ROOT, "docs", "progress.json")

def module_key(name):
    """Keep historical EXE keys, and preserve distinct COM/117/LOG identities."""
    return name.upper().removesuffix(".EXE")

def matched_addresses():
    text = open(os.path.join(ROOT, "src", "matched", "matched.c"), encoding="utf-8").read()
    return {(module_key(mod), int(seg, 16) * 16 + int(ip, 16))
            for mod, seg, ip in re.findall(r'"(\w+\.(?:EXE|COM|117|LOG))", \w+, 0x([0-9A-F]{4}), 0x([0-9A-F]{4})', text)}


def fixes_share():
    fixes = open(os.path.join(ROOT, "src", "fixes", "fixes.c"), encoding="utf-8").read()
    table = fixes[fixes.index("FIXES[] = {"):]
    available = set(re.findall(r'\{\s*"(D\d+)",', table[:table.index("};")]))
    bugs = open(os.path.join(ROOT, "docs", "bugs.md"), encoding="utf-8").read()
    # A D report is a ### section, or a #### entry inside a group of the Q-series section.
    sections = dict(re.findall(r"^####?\s+(D\d+)\.[^\n]*\n(.*?)(?=^#{2,4} |\Z)", bugs, re.M | re.S))
    closed = {key for key, body in sections.items()
              if re.search(r"^- \*\*Disposition\.\*\* Closed:", body, re.M)}
    actionable = set(sections) - closed
    fixed = available & actionable
    for key in actionable:
        alias = re.search(r"^- \*\*Fixed by:\*\* (D\d+)\.", sections[key], re.M)
        if alias and alias[1] in available:
            fixed.add(key)
    return len(fixed) / max(len(actionable), 1), len(fixed), len(actionable), len(closed)



def named_bytes(cover_dir, data_dir, route_dir):
    """Every code byte a matched body runs in place of the original, by module and linear address.

    Two kinds of evidence, as for the proofs themselves. The lockstep: func_lockstep
    (F117_NAMED_COVER=FILE) writes, per matched routine, the original instructions the routine
    runs at its own level - not inside routines it calls, not after it hands back to the
    original - over every compared state, and the addresses where it handed back. The routes:
    a routine's own code is walked from its entry (jumps followed, calls stepped over, stopping
    at a return, a computed jump or a hand-back address), and its instructions count where a
    route ran them (interpreter --coverage captures of every route). So a body that names one
    RET earns that RET, and a body that runs a shared tail or another census entry's code
    inline earns those bytes once."""
    sys.path.insert(0, os.path.join(ROOT, "recompiler"))
    from modules import load_all
    from x86dec import decode
    mods = {module_key(m.name): m for m in load_all(data_dir)}
    lines = [l for path in sorted(glob.glob(os.path.join(cover_dir, "*.cov")))
             for l in open(path, encoding="utf-8")]
    routed = set()
    for path in sorted(glob.glob(os.path.join(route_dir, "*.cov"))):
        for line in open(path, encoding="utf-8"):
            parts = line.split()
            if len(parts) >= 4:   # by segment and offset: a packed program's unpacker shares linear addresses
                routed.add((module_key(parts[0]), int(parts[2], 16), int(parts[3], 16)))
    return cover_bytes(lines, lambda module, seg, ip: decode(mods[module].image, mods[module].off(seg, ip), ip),
                       matched_addresses(), routed)


def own_walk(insn, module, seg, entry, stops, limit=4000):
    """The routine's own instructions reachable from its entry: jumps followed, calls stepped over;
    a return, a computed or far jump, an undecodable byte or a hand-back address ends a path
    (at most `limit` instructions, as tools/matched_draft.py walks)."""
    sys.path.insert(0, os.path.join(ROOT, "recompiler"))
    from x86dec import DecodeError, K_HLT, K_INVALID, K_JCC, K_JMP, K_JMPFAR, K_JMPIND, K_RET
    seen, todo = {}, [entry]
    while todo:
        ip = todo.pop()
        while ip not in seen and ip not in stops and len(seen) < limit:
            try:
                ins = insn(module, seg, ip)
            except DecodeError:   # ran off the image
                break
            seen[ip] = ins
            if ins.kind in (K_RET, K_JMPIND, K_JMPFAR, K_HLT, K_INVALID):
                break
            if ins.kind == K_JMP:
                ip = ins.target & 0xFFFF
                continue
            if ins.kind == K_JCC:
                todo.append(ins.target & 0xFFFF)
            ip = ins.next_ip & 0xFFFF
    return seen


def cover_bytes(lines, insn, matched, routed):
    """Named-coverage lines ("MODULE.EXE SEG IP N: ip ip ... | hand-back ip ...") and the route
    captures' instructions (module, segment, offset) to covered bytes by module and linear
    address; every matched routine must have a line, or its credit would silently vanish."""
    covered, seen = {}, set()
    for line in lines:
        head, _, rest = line.partition(":")
        named, _, hand = rest.partition("|")
        exe, seg, entry, _compared = head.split()
        module, seg, entry = module_key(exe), int(seg, 16), int(entry, 16)
        seen.add((module, seg * 16 + entry))
        out = covered.setdefault(module, set())
        for ip in (int(x, 16) for x in named.split()):
            out.update(range(seg * 16 + ip, seg * 16 + ip + insn(module, seg, ip).length))
        stops = {int(x, 16) for x in hand.split()}
        for ip, ins in own_walk(insn, module, seg, entry, stops).items():
            if (module, seg, ip) in routed:
                out.update(range(seg * 16 + ip, seg * 16 + ip + ins.length))
    missing = sorted(matched - seen)
    if missing:
        raise SystemExit("named coverage lacks %d matched routines, e.g. %s" % (len(missing), missing[:5]))
    return covered


def refresh_census(data, path, cover_dir, data_dir, route_dir):
    with open(path, encoding="utf-8") as source:
        rows = [l.rstrip("\n").split("\t") for l in source][1:]
    covered = named_bytes(cover_dir, data_dir, route_dir)
    c = data["census"]
    c["functions"] = len(rows)
    c["bytes"] = sum(int(r[2]) for r in rows)
    credit = [sum(1 for b in range(int(r[1], 16), int(r[1], 16) + int(r[2])) if b in covered.get(r[0], ()))
              for r in rows]
    c["matched_bytes"] = sum(credit)
    c["matched_functions"] = sum(1 for r, n in zip(rows, credit) if n == int(r[2]))
    c["partial_functions"] = sum(1 for r, n in zip(rows, credit) if 0 < n < int(r[2]))


def refresh_classes(data, path, cover_dir, data_dir, route_dir):
    """The denominator is split (tools/census_classify.py): data and code that stays original cannot earn
    credit. Run after refresh_census; it decodes the game's programs, which the credit step does not need."""
    import census_classify
    c = data["census"]
    scope, detail = {}, {}
    _, totals, _ = census_classify.run(path, cover_dir, route_dir, data_dir, scope=scope, detail=detail)
    assert totals["credited"] == c["matched_bytes"], "census_classify and progress disagree on the credit"
    assert sum(totals.values()) == c["bytes"], "census_classify and progress disagree on the census size"
    c["classes"] = {k: totals[k] for k in census_classify.CLASSES}
    c["creditable_bytes"] = c["bytes"] - totals["data"] - totals["original"] - totals["dead"]
    c["supplemental"] = scope
    return detail


def refresh_gameplay(data, detail, reference=None):
    """The gameplay score (owner, 21 Oct 2026; census_classify.gameplay): executed bytes only. Run after
    refresh_classes, from the byte sets it collected; the previous all-code measure stays in data["census"]."""
    import census_classify as cc
    c = data["census"]
    rows = detail["rows"]
    original = cc.read_original()
    entries = cc.read_reference_reviewed(reference or cc.REFERENCE_REVIEWED)
    cc.check_design_rule(entries, rows, original)
    g = cc.gameplay(detail["covered"], detail["ran"], detail["scope_bytes"], cc.original_bytes(rows, original),
                    entries, cc.modules_scope())
    # The previous scope, counted by row occurrence (204,747 on 21 Oct), is its distinct bytes plus the bytes
    # two census rows share; the checklist and the cross-check below use distinct bytes.
    occurrences = c["creditable_bytes"] + c["supplemental"]["bytes"]
    x = g["crosscheck"]
    x["previous_scope_occurrences"] = occurrences
    x["previous_scope_shared_row_bytes"] = occurrences - x["previous_scope_distinct"]
    assert x["previous_scope_shared_row_bytes"] >= 0, "previous scope has fewer occurrences than distinct bytes"
    assert x["denominator_in_previous_scope"] + g["checklist"]["bytes"] == x["previous_scope_distinct"]
    assert (g["matched_route_run"] + g["stays_original_reviewed"] + g["remaining"]) == g["denominator"]
    c["gameplay"] = g


def compute(data):
    c = data["census"]
    share, fixed, catalogued, closed = fixes_share()
    supplemental = c.get("supplemental", {})
    measures = {
        "census_matched_bytes": ((c["matched_bytes"] + supplemental.get("credited", 0)) /
                                 (c.get("creditable_bytes", c["bytes"]) + supplemental.get("bytes", 0))),
        "fixes_share": share,
    }
    if "gameplay" in c:
        g = c["gameplay"]
        measures["gameplay_score"] = (g["matched_route_run"] + g["stays_original_reviewed"]) / g["denominator"]
        if "checklist" in g:
            k = g["checklist"]
            reviewed, total = k["with_reviewed_entry"], k["bytes"]
            if not 0 <= reviewed <= total or k["without_reviewed_entry"] != total - reviewed:
                raise ValueError("accounted-for checklist counts do not partition its scope")
            measures["accounted_for"] = reviewed / total if total else 1.0
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
    return out, rows, (fixed, catalogued, closed)


def title(pct):
    return "(%s, All %.2f%%)" % (", ".join("%s %.2f%%" % (p, pct[p]) for p in sorted(pct) if p != "All"), pct["All"])


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--title", action="store_true", help="print only the commit-title suffix")
    ap.add_argument("--census", help="reimp_names.py TSV: refresh the census figures in progress.json")
    ap.add_argument("--named-cover", help="with --census: directory of func_lockstep F117_NAMED_COVER files")
    ap.add_argument("--route-cover", help="with --census: directory of interpreter --coverage captures of every route")
    ap.add_argument("--data", help="with --census: the game directory, to decode instructions")
    a = ap.parse_args()
    data = json.load(open(DATA, encoding="utf-8"))
    if a.census:
        if not (a.named_cover and a.route_cover and a.data):
            ap.error("--census needs --named-cover, --route-cover and --data: credit is by the bytes named code runs")
        refresh_census(data, a.census, a.named_cover, a.data, a.route_cover)
        refresh_gameplay(data, refresh_classes(data, a.census, a.named_cover, a.data, a.route_cover))
        with open(DATA, "w", encoding="utf-8", newline="\n") as f:
            json.dump(data, f, indent=2)
            f.write("\n")
    pct, rows, (fixed, catalogued, closed) = compute(data)
    if a.title:
        print(title(pct))
        return
    for phase, item, weight, done, kind in rows:
        print("%-3s %5.1f%% of weight %5.1f  %-9s %s" % (phase, 100 * done, weight, kind, item))
    c = data["census"]
    if "gameplay" in c:
        g = c["gameplay"]
        print("gameplay score (P2 'Matched functions replacing translations'): %.2f%% of %d executed bytes = %d matched "
              "bytes a route runs + %d reviewed design-rule original bytes"
              % (100.0 * (g["matched_route_run"] + g["stays_original_reviewed"]) / g["denominator"], g["denominator"],
                 g["matched_route_run"], g["stays_original_reviewed"]))
        print("  fixture-only matched bytes (no route runs them; not counted): %d" % g["fixture_only"])
        print("  remaining executed, unmatched bytes: %d (of them %d design-rule original awaiting a reviewed entry); by module: %s"
              % (g["remaining"], g["original_awaiting_review"],
                 ", ".join("%s %d" % (m, v["remaining"]) for m, v in g["by_module"].items() if v["remaining"])))
        k = g["checklist"]
        print("  accounted for: %d of %d bytes of the previous all-code scope outside the denominator have a reviewed "
              "reference entry (%.2f%%; target 100%%), %d without (docs/reference-reviewed.tsv, %d entries)"
              % (k["with_reviewed_entry"], k["bytes"],
                 100.0 * k["with_reviewed_entry"] / k["bytes"] if k["bytes"] else 100.0,
                 k["without_reviewed_entry"], g["reviewed_entries"]))
    print("census: %d of %d creditable bytes (%d in all; data, original and certified-dead code left out) run by named code; %d of %d functions whole, %d in part; fixes %d of %d actionable reports; %d historical reports closed without fixes"
          % (c["matched_bytes"], c.get("creditable_bytes", c["bytes"]), c["bytes"], c["matched_functions"], c["functions"],
             c.get("partial_functions", 0), fixed, catalogued, closed))
    if "supplemental" in c:
        s = c["supplemental"]
        print("previous measure (all-code scope; P2 'Routines named and explained'): %d of %d creditable bytes (%.2f%%); adds %d bytes outside census (%d named, %d reviewed static); unreviewed static discoveries remain outside this measure"
              % (c["matched_bytes"] + s["credited"], c["creditable_bytes"] + s["bytes"],
                 100.0 * (c["matched_bytes"] + s["credited"]) / (c["creditable_bytes"] + s["bytes"]), s["bytes"],
                 s["credited"], s.get("reviewed_static_bytes", 0)))
    print(title(pct))


if __name__ == "__main__":
    main()
