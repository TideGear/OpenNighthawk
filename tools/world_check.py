#!/usr/bin/env python3
"""Check tools/world_decode.py against the game's own memory (Remastered stage R1).

Usage: world_check.py GAMEDIR DUMP.bin [STEM ...]

DUMP.bin is a memory dump from a run that has loaded a theatre (f117run --dump 0:100000). For each theatre,
the decoded terrain tables are searched for in the dump as START's loader lays them out in memory
(DS:B3A0 .. B9A0 .. BAA0, contiguous: the three 512-byte tables in reverse file order, then the level-3
map, then the level-4 map), and the point table is searched for at DS:BAB0 (7 bytes a point: three words
and the kept byte, entries' points consecutive in list order). A theatre is reported as found when the
whole image matches.
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import world_decode as wd  # noqa: E402


def tables_image(g):
    t = g["tables"]                       # file order: B7A0, B5A0, B3A0
    return t[2] + t[1] + t[0] + g["level3"] + g["level4"]


def points_image(t):
    out = bytearray()
    for lst in t["lists"]:
        for entry in lst:
            for x, y, z, b in entry:
                out += struct.pack("<3hB", x, y, z, b)
    return bytes(out)


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 2
    game, dump = argv[1], open(argv[2], "rb").read()
    stems = argv[3:] or wd.THEATRES
    found = 0
    for stem in stems:
        with open(os.path.join(game, stem + ".3DG"), "rb") as f:
            g = wd.decode_3dg(f.read())
        with open(os.path.join(game, stem + ".3DT"), "rb") as f:
            t = wd.decode_3dt(f.read())
        img = tables_image(g)
        at = dump.find(img)
        if at < 0:
            print("%s: terrain tables not in the dump" % stem)
            continue
        found += 1
        # the point table starts at DS:BAB0, 16 bytes after the level-4 map's start (BAA0)
        pts = points_image(t)
        pstart = at + len(img)             # the level-4 map ends at BAB0
        ok = dump[pstart:pstart + len(pts)] == pts
        print("%s: terrain tables at %#x (DS:B3A0 .. BAB0); point table %d bytes %s"
              % (stem, at, len(pts), "match" if ok else "DIFFERS"))
        if not ok:
            return 1
    if not found:
        print("no theatre's tables found: the dump holds no loaded theatre")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
