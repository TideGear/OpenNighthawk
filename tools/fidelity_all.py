#!/usr/bin/env python3
"""fidelity_all.py - the machine-behaviour probe against every reference PC.

    py tools/fidelity_all.py --data GOG_DIR [--work DIR] [--86box-run | --86box-bin FILE]
                             [--update-baseline] [--no-gog] [--no-dosbox-x] [--no-86box]

Runs tools/fidelity.py's probe (about 1,200 answers: DOS memory, PSP and
environment, every DOS/BIOS service's registers, BIOS data, VGA registers,
devices, clocks and what operations cost in time) on this machine and on GOG's
DOSBox 0.74, on DOSBox-X and in 86Box, and compares this machine with each.

The machines legitimately differ in places (DOSBox-X lays DOS memory out its
own way; 86Box is a 386DX/33 with a real BIOS and MS-DOS 5.00), so the verdict is against a
stored baseline, tools/fidelity_baseline.json: per reference, the fields that
are known to differ. The check fails when a field outside the baseline starts
to differ (a regression) and reports, without failing, a known difference that
has gone away (a fix to record with --update-baseline). The baseline for GOG's
DOSBox is empty: every answer agrees.

Fields about speed (rates, costs in PIT counts) are compared only for the
DOSBoxes, which run at this machine's modelled speed; against 86Box they are
listed as informational. Answers that only describe how a DOS and BIOS lay
memory out (segments, PSP, the memory chain, the child's frame) are judged
against GOG's DOSBox alone, which this machine follows.
"""
import argparse
import json
import os
import re
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import fidelity  # noqa: E402

BASELINE = os.path.join(HERE, "fidelity_baseline.json")
DBX = r"D:\86box-src\dbx-src\src\dosbox-x.exe"


# Answers that describe how a machine lays DOS out in memory - segment values at each call, the PSP,
# the memory chain, the EXEC'd child's frame - not what the game can do. GOG's DOSBox is the one reference
# held to them (this machine follows it, and the game was run on it); another DOS and another BIOS place
# things differently, so for the others they are left out of the verdict and out of the baseline.
LAYOUT = re.compile(r"( DS| ES)$|^psp |^mcb |^child|environment segment|segment$")


def field_ok(kind, r, o):
    if kind == "rate":
        return abs(r - o) <= max(2, r // 50)
    if kind == "rate5":
        return abs(r - o) <= max(2, r // 20)
    if kind == "nonzero":
        return (r != 0) == (o != 0)
    if kind == "zeroish":
        return abs((r ^ 0x8000) - (o ^ 0x8000)) <= 2
    return r == o


def differing(ref, ours, n, ref_child, ours_child, nc, speed_fields, layout=True):
    """Names of the answers where `ref` and `ours` disagree (layout answers only when `layout`)."""
    out = []
    for k, (name, kind) in enumerate(fidelity.FIELDS):
        if kind == "ignore":
            continue
        r = struct.unpack_from("<H", ref, 2 * k)[0]
        o = struct.unpack_from("<H", ours, 2 * k)[0]
        if not field_ok(kind, r, o):
            if not speed_fields and (kind in ("rate", "rate5") or name.startswith("PIT counts") or "per VGA frame" in name):
                continue
            out.append(name)
    for k, (name, _kind) in enumerate(fidelity.CHILD_FIELDS):
        if struct.unpack_from("<H", ref_child, 2 * k)[0] != struct.unpack_from("<H", ours_child, 2 * k)[0]:
            out.append("child: " + name)
    off = 2 * n + fidelity.MCB_BYTES
    out.extend("psp %02X" % i for i in range(0x100) if ref[off + i] != ours[off + i] and not (0x2C <= i < 0x2E))
    co = 2 * nc + fidelity.MCB_BYTES
    out.extend("child psp %02X" % i for i in range(0x100) if ref_child[co + i] != ours_child[co + i])
    for sheet_n, a, b, label in ((n, ref, ours, "mcb"), (nc, ref_child, ours_child, "child mcb")):
        for k in range(24):
            ea = a[2 * sheet_n + 18 * k: 2 * sheet_n + 18 * k + 18]
            eb = b[2 * sheet_n + 18 * k: 2 * sheet_n + 18 * k + 18]
            if ea != eb:
                out.append("%s %d" % (label, k))
    if not layout:
        out = [x for x in out if not LAYOUT.search(x)]
    return sorted(set(out))


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--data", required=True)
    ap.add_argument("--work", default=os.path.join(os.path.expanduser("~"), "f117-recomp-local", "fidelity-all"))
    ap.add_argument("--86box-run", dest="run86", action="store_true", help="run the probe in 86Box now")
    ap.add_argument("--86box-bin", dest="bin86", help="a saved 86Box answer sheet (86box.bin; 86box-child.bin beside it)")
    ap.add_argument("--update-baseline", action="store_true")
    for name in ("gog", "dosbox-x", "86box"):
        ap.add_argument("--no-" + name, action="store_true")
    a = ap.parse_args()
    os.makedirs(a.work, exist_ok=True)
    probe, n = fidelity.build_probe()
    child, nc = fidelity.build_child()
    ours, ours_child = fidelity.run_ours(probe, a.work, child)
    sheets = {}
    if not a.no_gog:
        sheets["gog"] = fidelity.run_dosbox(os.path.join(a.data, "DOSBOX", "DOSBox.exe"), a.data, probe, a.work, child, "dosbox")
    if not a.no_dosbox_x:
        sheets["dosbox-x"] = fidelity.run_dosbox(DBX, a.data, probe, a.work, child, "dosbox-x")
    if not a.no_86box:
        folder = os.path.join(a.work, "86box")
        if a.run86:
            subprocess.run([sys.executable, os.path.join(HERE, "ref86box", "probe86.py"), folder], check=True)
            path = os.path.join(folder, "86box.bin")
        else:
            path = a.bin86 or os.path.join(folder, "86box.bin")
        if os.path.exists(path):
            sheets["86box"] = (open(path, "rb").read(), open(os.path.join(os.path.dirname(path), "86box-child.bin"), "rb").read())
        else:
            sys.exit("no 86Box answer sheet at %s (use --86box-run)" % path)
    baseline = json.load(open(BASELINE)) if os.path.exists(BASELINE) else {}
    found, failed = {}, []
    for name, (ref, ref_child) in sheets.items():
        found[name] = differing(ref, ours, n, ref_child, ours_child, nc, speed_fields=name != "86box", layout=name == "gog")
        known = set(baseline.get(name, []))
        new, gone = sorted(set(found[name]) - known), sorted(known - set(found[name]))
        print("%-9s %4d answers differ from this machine; baseline %d; new %d; gone %d" % (
            name, len(found[name]), len(known), len(new), len(gone)))
        for f in new:
            print("    NEW  ", f)
        for f in gone:
            print("    gone ", f)
        if new and not a.update_baseline:
            failed.append(name)
    if a.update_baseline:
        baseline.update(found)
        json.dump(baseline, open(BASELINE, "w"), indent=1, sort_keys=True)
        print("baseline updated:", BASELINE)
        return 0
    print("RESULT:", "no new differences" if not failed else "NEW DIFFERENCES against " + ", ".join(failed))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
