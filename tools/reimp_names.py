#!/usr/bin/env python3
"""reimp_names.py - name the translated routines from the Reimp's reading.

    py tools/reimp_names.py --reimp "..\\F-117A Reimp" --gen GEN_DIR --out FILE.tsv

Phase 2 names the translated code. The Reimp (a separate project, read here
and never written) has already read much of the original: its census
(tools/f117/audit.py) lists every function the disassembler reaches in each
program, by image offset, and its sources cite the original addresses they
reproduce in comments (tools/f117/addrmap.py). This joins the two with the
regions this recompilation generated (R_<MODULE>_<SEG>_<OFF>, image offset
SEG*16+OFF), and writes one row per census function:

  implemented_by  Reimp functions whose doc comment - the comment block just
                  above the definition - cites the address first: its
                  reading of that routine, and the strongest naming lead.
  cited_by        every Reimp function whose comments mention the address,
                  callers included: weaker leads.

A citation is a reading, not a proof. The same number can be an entry in
two programs, and the Reimp keys a citation to a program only when its prose
says which. Where the prose names programs and this one is not among them,
the citation is dropped; where it names none and the address is an entry in
another program too, the row is marked ambiguous.

The output quotes the Reimp's function names and paths: keep it private,
outside the repository.
"""
from __future__ import annotations

import argparse
import collections
import os
import re
import sys

BINARIES = ("VGAME", "START", "END", "PLAYER", "MPS_LOGO", "DSWAP", "SETUP")
REGION = re.compile(r"\bR_([A-Z0-9_]+?)_EXE_([0-9A-F]{4})_([0-9A-F]{4})\b")


def key(addr):
    return ("%X" % addr).rjust(4, "0")


def implementers(root, addrmap):
    """{address key: {function}} from doc comments: a comment block followed
    directly (blank lines aside) by a function definition."""
    out = collections.defaultdict(set)
    for base, dirs, files in os.walk(os.path.join(root, "src")):
        dirs[:] = sorted(dirs)
        for f in sorted(files):
            if not f.endswith((".c", ".h")):
                continue
            text = open(os.path.join(base, f), encoding="utf-8", errors="replace").read()
            for a, b in addrmap.comment_spans(text):
                line = text[b:b + 400].lstrip().split("\n", 1)[0]
                m = addrmap.FUNC.match(line)
                if not m or line.rstrip().endswith(";"):
                    continue
                chunk = text[a:b]
                ds = {x.group(1).upper().lstrip("0").rjust(4, "0")
                      for x in addrmap.BRACKETED.finditer(chunk)}
                # The first address a doc comment cites is the routine it
                # describes; later ones are what it calls or reads.
                for x in addrmap.ADDR.finditer(chunk):
                    plain = x.group(1) or x.group(4) or x.group(5)
                    if not plain or int(plain, 16) < 0x100:
                        continue
                    k = plain.upper().lstrip("0").rjust(4, "0")
                    if k not in ds:
                        out[k].add(m.group(1))
                        break
    return out


def our_regions(gen):
    """{binary: {image offset: region name}} from the generated tables."""
    out = collections.defaultdict(dict)
    for f in os.listdir(gen):
        if not f.endswith("_tab.c"):
            continue
        with open(os.path.join(gen, f), encoding="utf-8", errors="replace") as fh:
            for m in REGION.finditer(fh.read()):
                out[m.group(1)][int(m.group(2), 16) * 16 + int(m.group(3), 16)] = m.group(0)
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--reimp", required=True, help="the Reimp checkout (read only)")
    ap.add_argument("--gen", required=True, help="this project's generated C directory")
    ap.add_argument("--out", required=True, help="TSV to write (outside the repository)")
    a = ap.parse_args()

    root = os.path.abspath(a.reimp)
    sys.path.insert(0, os.path.join(root, "tools", "f117"))
    import addrmap  # noqa: E402  (the Reimp's, read only)
    import audit    # noqa: E402

    impl = implementers(root, addrmap)
    hits = addrmap.scan()                       # address -> [(file, line, func, is_ds)]
    named = audit.named_binaries_map()          # address -> programs its prose names
    census = {b: audit.functions(b) for b in BINARIES}
    entries = {b: {e for e, _ in fs} for b, fs in census.items()}
    regions = our_regions(a.gen)

    rows, t = [], collections.Counter()
    for b in BINARIES:
        for entry, size in sorted(census[b]):
            prose = named.get(entry, [])
            mine = not prose or b in prose
            cites = [h for h in hits.get(key(entry), []) if not h[3]] if mine else []
            implemented = sorted(impl.get(key(entry), set())) if mine else []
            others = [o for o in BINARIES if o != b and entry in entries[o]]
            ambiguous = bool(cites or implemented) and bool(others) and not prose
            funcs = sorted({c[2] for c in cites if c[2]})
            files = sorted({"%s:%d" % (c[0], c[1]) for c in cites})[:4]
            region = regions.get(b, {}).get(entry, "")
            t[b, "functions"] += 1
            t[b, "bytes"] += size
            t[b, "translated"] += bool(region)
            t[b, "implemented"] += bool(implemented)
            t[b, "implemented_bytes"] += size if implemented else 0
            t[b, "cited"] += bool(funcs)
            t[b, "ambiguous"] += ambiguous
            rows.append((b, "0x%05X" % entry, size, region, ";".join(implemented),
                         ";".join(funcs), "ambiguous:" + ",".join(others) if ambiguous else "",
                         " ".join(files)))

    os.makedirs(os.path.dirname(os.path.abspath(a.out)), exist_ok=True)
    with open(a.out, "w", encoding="utf-8", newline="\n") as f:
        f.write("binary\tentry\tsize\tour_region\timplemented_by\tcited_by\tnote\tcited_at\n")
        for r in rows:
            f.write("\t".join(str(x) for x in r) + "\n")

    print("binary     functions  translated  implemented (bytes)       cited  ambiguous")
    for b in BINARIES:
        n, by = t[b, "functions"], t[b, "bytes"]
        print("%-10s %9d  %10d  %5d %5.1f%% (%5.1f%%)  %10d  %9d" % (
            b, n, t[b, "translated"], t[b, "implemented"], 100.0 * t[b, "implemented"] / max(n, 1),
            100.0 * t[b, "implemented_bytes"] / max(by, 1), t[b, "cited"], t[b, "ambiguous"]))
    print("wrote", a.out)


if __name__ == "__main__":
    main()
