#!/usr/bin/env python3
"""census.py - how much of each module's code area the translation covers.

    py tools/census.py --data DIR [--coverage FILE ...] [--gaps N]

For each module: the bytes of its code area (an EXE's segments below its
data group, an overlay's image after the descriptor, a .COM's image) that
are inside a translated instruction, and the gaps that are not. Each gap is
classified by trying to decode it: "code?" if it decodes cleanly to a
return or jump with few zero bytes, "data" otherwise. A gap that looks like
code is a candidate the discovery missed; anything it misses still runs,
interpreted, but the census is how "the whole game is translated" is
measured rather than assumed.
"""
from __future__ import annotations

import argparse
import glob
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(HERE), "recompiler"))

from discover import Discovery                        # noqa: E402
from modules import load_all                          # noqa: E402
from recomp import read_coverage                      # noqa: E402
from x86dec import DecodeError, K_RET, K_JMP, K_JMPFAR, K_INVALID, decode   # noqa: E402


def code_spans(m, covered):
    """An EXE's code area: the segments below its data group, split at every
    segment value the loader relocates, keeping the spans that hold a
    translated instruction. Microsoft C places far data segments (tables)
    between the code and the data group; a span nothing executes or branches
    into is one of those."""
    lo, hi = 0, min(len(m.image), m.dgroup * 16)
    bounds = sorted({lo, hi} | {struct.unpack("<H", m.image[r:r + 2])[0] * 16 for r in m.relocs
                                if r + 2 <= len(m.image)} & set(range(lo, hi + 1)))
    out = []
    for a, b in zip(bounds, bounds[1:]):
        if any(covered[a:b]):
            out.append((a, b))
    return out


def code_area(m):
    if m.kind == "overlay" and m.entries:
        # the descriptor and the driver's data come before its base segment
        return min(len(m.image), m.entries[0][0] * 16), len(m.image)
    return 0, len(m.image)


def classify(img, start, end):
    """Does [start, end) decode like code from its first byte?"""
    zeros = sum(1 for b in img[start:end] if b == 0)
    if zeros * 3 > (end - start):
        return "data (mostly zero)"
    pos, n = start, 0
    while pos < end and n < 64:
        try:
            ins = decode(img, pos, pos & 0xFFFF, end)
        except DecodeError:
            return "data (does not decode)"
        if ins.kind == K_INVALID:
            return "data (invalid opcode)"
        n += 1
        pos += ins.length
        if ins.kind in (K_RET, K_JMP, K_JMPFAR):
            return "code? (%d instructions to a %s)" % (n, ins.kind)
    return "unclear"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--data", required=True)
    ap.add_argument("--coverage", action="append", default=None)
    ap.add_argument("--gaps", type=int, default=8)
    a = ap.parse_args()
    covs = a.coverage
    if covs is None:
        covs = sorted(glob.glob(os.path.join(os.path.expanduser("~"), "f117-recomp-local", "coverage", "*.cov")))
    mods = load_all(a.data)
    seeds, _ = read_coverage(covs, mods)
    tot_area = tot_cov = 0
    for m in mods:
        regions = Discovery(m, log=lambda *x: None).run(coverage=seeds.get(m.name, []))
        covered = bytearray(len(m.image))
        for r in regions:
            for ip, ins in r.insns.items():
                off = m.off(r.seg, ip)
                for k in range(ins.length):
                    if 0 <= off + k < len(covered):
                        covered[off + k] = 1
        if m.kind == "exe" and m.dgroup:
            spans = code_spans(m, covered)
        else:
            spans = [code_area(m)]
        area = sum(hi - lo for lo, hi in spans)
        cov = sum(sum(covered[lo:hi]) for lo, hi in spans)
        tot_area += area
        tot_cov += cov
        gaps = []
        for lo, hi in spans:
            i = lo
            while i < hi:
                if covered[i]:
                    i += 1
                    continue
                j = i
                while j < hi and not covered[j]:
                    j += 1
                gaps.append((j - i, i, j))
                i = j
        codey = [g for g in gaps if g[0] >= 8 and classify(m.image, g[1], g[2]).startswith("code?")]
        print("%-14s code area %6d bytes, %6d translated (%5.1f%%), %4d gaps, %3d look like code (%d bytes)" % (
            m.name, area, cov, 100.0 * cov / area if area else 0, len(gaps), len(codey), sum(g[0] for g in codey)))
        for size, s, e in sorted(gaps, reverse=True)[:a.gaps]:
            print("    gap %05X-%05X %5d bytes  %s" % (s, e, size, classify(m.image, s, e)))
    print("all modules: %d of %d code-area bytes inside translated instructions (%.1f%%)" % (
        tot_cov, tot_area, 100.0 * tot_cov / tot_area if tot_area else 0))


if __name__ == "__main__":
    main()
