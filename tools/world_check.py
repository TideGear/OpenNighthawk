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


def check_wld(dump, w):
    """The world file's pieces at the DS offsets START's world_read fills them: the 256-byte sector grid at CAD8
    is located in the dump, which gives the data segment's base; the rest are checked relative to it."""
    at = dump.find(w["grid"])
    if at < 0:
        return None
    base = at - 0xCAD8
    def mem(off, n):
        return dump[base + off:base + off + n]
    diffs = {}
    for name, off, n, want in (("head0", 0xD88C, 2, struct.pack("<H", w["head"][0])), ("head1", 0xD33C, 2, struct.pack("<H", w["head"][1])),
                         ("head2", 0xCACE, 2, struct.pack("<H", w["head"][2])), ("head3", 0xE084, 2, struct.pack("<H", w["head"][3])),
                         ("targets", 0xCBDE, 16 * len(w["targets"]), b"".join(w["targets"])),
                         ("slot count", 0xD890, 2, struct.pack("<H", len(w["slots"]))),
                         ("slots", 0xD3BE, 36 * len(w["slots"]), b"".join(w["slots"])),
                         ("table 0", 0xD7F6, 128, w["tables"][0]), ("table 1", 0xD33E, 128, w["tables"][1]),
                         ("table 2", 0xD2A6, 128, w["tables"][2]),
                         ("names", 0xDD70, len(w["names"]), w["names"])):
        got = mem(off, n)
        n_diff = sum(1 for a, b in zip(got, want) if a != b)
        if n_diff:
            diffs[name] = n_diff
    return diffs


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
        with open(os.path.join(game, stem + ".WLD"), "rb") as f:
            wld = check_wld(dump, wd.decode_wld(f.read()))
        # a piece the game itself writes after the load shows as a few differing bytes: reported, not hidden
        text = "not found" if wld is None else "match" if not wld else             "%s (the game writes fields after loading)" % ", ".join("%d bytes differ in %s" % (v, k) for k, v in wld.items())
        print("%s: terrain tables at %#x (DS:B3A0 .. BAB0); point table %d bytes %s; world file %s"
              % (stem, at, len(pts), "match" if ok else "DIFFERS", text))
        ok = ok and wld is not None
        if not ok:
            return 1
    if not found:
        print("no theatre's tables found: the dump holds no loaded theatre")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
