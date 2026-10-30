#!/usr/bin/env python3
"""Decode a theatre's terrain files without the game (Remastered stage R1, docs/remastered-design.md).

The layouts are the ones START's matched routines read (src/matched/matched.c):

  STEM.3DG  start_terrain_tables (START 0x07D71): the mark "22", then the level-4 map (16 bytes), the
            level-3 map (256 bytes) and three 512-byte tables.
  STEM.3DT  start_theatre_lists (START 0x07E88): the mark "11", five list sizes (words, each at most 32),
            then each list's point counts (a word per entry), then for each entry its points: three words
            and a word whose low byte is kept (fread(buf, 2, 1): 8 bytes in the file, stored 7 bytes apart).

Usage: world_decode.py GAMEDIR [STEM ...]   (default: every theatre)
Prints each file's decoded sizes and checks that the layout accounts for every byte of the file.
"""
import os
import struct
import sys

THEATRES = ["CE", "CU", "KO", "KU", "LB", "ME", "NC", "PG", "VN"]


def decode_3dg(data):
    if data[:2] != b"22":
        raise ValueError("3DG: mark is not '22'")
    sizes = [16, 256, 512, 512, 512]
    parts, at = [], 2
    for n in sizes:
        parts.append(data[at:at + n])
        at += n
    if at != len(data):
        raise ValueError("3DG: %d bytes left over" % (len(data) - at))
    return {"level4": parts[0], "level3": parts[1], "tables": parts[2:]}


def decode_3dt(data):
    if data[:2] != b"11":
        raise ValueError("3DT: mark is not '11'")
    sizes = list(struct.unpack_from("<5H", data, 2))
    if any(s > 0x20 for s in sizes):
        raise ValueError("3DT: a list over 20h entries: %r" % sizes)
    at = 12
    counts = []
    for s in sizes:
        counts.append(list(struct.unpack_from("<%dH" % s, data, at)))
        at += 2 * s
    lists = []
    for i, cs in enumerate(counts):
        entries = []
        for n in cs:
            pts = []
            for _ in range(n):
                x, y, z, b = struct.unpack_from("<3hH", data, at)
                pts.append((x, y, z, b & 255))
                at += 8
            entries.append(pts)
        lists.append(entries)
    if at != len(data):
        raise ValueError("3DT: %d bytes left over" % (len(data) - at))
    return {"sizes": sizes, "lists": lists}


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    game = argv[1]
    stems = argv[2:] or THEATRES
    bad = 0
    for stem in stems:
        line = [stem]
        for ext, fn in (("3DG", decode_3dg), ("3DT", decode_3dt)):
            path = os.path.join(game, "%s.%s" % (stem, ext))
            try:
                with open(path, "rb") as f:
                    d = fn(f.read())
                if ext == "3DT":
                    pts = sum(len(e) for l in d["lists"] for e in l)
                    line.append("3DT sizes %s, %d points" % (d["sizes"], pts))
                else:
                    line.append("3DG ok")
            except (OSError, ValueError, struct.error) as e:
                line.append("%s FAILED: %s" % (ext, e))
                bad += 1
        print("  ".join(line))
    print("%d file(s) did not decode" % bad if bad else "every file's bytes are fully accounted for")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
