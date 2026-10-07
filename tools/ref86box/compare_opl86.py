#!/usr/bin/env python3
"""compare_opl86.py - this machine's AdLib writes against 86Box's (sound86.py).

    py tools/ref86box/compare_opl86.py OURS_opl.log OPL86_log

OURS is an f117run --opl-log (guest clocks at 9 MIPS), the other the 86Box
log (emulated microseconds). The 86Box board is a 6 MHz 286, slower than the
model, so absolute times drift (the game's loops, not the PIT, pace parts of
the music) and the writes of the channel that carries pitch bends interleave
differently. What must hold, and is judged:

  - the first --prefix writes are identical, in order;
  - per channel, the sequence of key-on writes (register B0+n with bit 5) is
    identical on every channel that plays few notes (the melodic voices), and
    the count of key-ons agrees within 1% on every channel;
  - the number of channels whose key-on sequence is identical is not less
    than measured (4 of 9: 0, 1, 2 and 5; the busy channels 3 and 4 depend
    on speed).

The time offset between the machines at the first and last matched note is
printed for information. Exit 0 when the judged figures hold.
"""
import argparse
import difflib
import sys

IPS = 9_000_000
MIN_IDENTICAL_CHANNELS = 4          # measured 6 Oct 2026: channels 0, 1, 2, 5


def load(path, per_second):
    out = []
    for line in open(path):
        t, r, v = line.split()
        out.append((int(t) / per_second, int(r, 16), int(v, 16)))
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("ours")
    ap.add_argument("box")
    ap.add_argument("--prefix", type=int, default=2000)
    a = ap.parse_args()
    ours, box = load(a.ours, IPS), load(a.box, 1_000_000)
    pairs = lambda x: [(r, v) for _, r, v in x]
    n = min(a.prefix, len(ours), len(box))
    prefix_ok = pairs(ours)[:n] == pairs(box)[:n]
    ok = prefix_ok
    identical = 0
    rows = []
    for ch in range(9):
        sa = [(t, v) for t, r, v in ours if r == 0xB0 + ch and v & 0x20]
        sb = [(t, v) for t, r, v in box if r == 0xB0 + ch and v & 0x20]
        count_ok = abs(len(sa) - len(sb)) <= max(1, len(sa) // 100)
        same = [v for _, v in sa] == [v for _, v in sb]
        identical += bool(same and sa)
        if not count_ok:
            ok = False
        rows.append((ch, len(sa), len(sb), same, count_ok))
    sparse_bad = [r[0] for r in rows if r[1] and r[1] < 600 and not r[3]]
    if sparse_bad:
        ok = False
    if identical < MIN_IDENTICAL_CHANNELS:
        ok = False
    ca = [(t, r, v) for t, r, v in ours if 0xB0 <= r <= 0xB8 and v & 0x20]
    cb = [(t, r, v) for t, r, v in box if 0xB0 <= r <= 0xB8 and v & 0x20]
    m = difflib.SequenceMatcher(None, [(r, v) for _, r, v in ca], [(r, v) for _, r, v in cb], autojunk=False)
    blocks = [b for b in m.get_matching_blocks() if b.size]
    drift = ""
    if blocks:
        first, last = blocks[0], blocks[-1]
        d0 = cb[first.b][0] - ca[first.a][0]
        d1 = cb[last.b + last.size - 1][0] - ca[last.a + last.size - 1][0]
        drift = "; offset at first/last matched note %+.2f / %+.2f s (informational)" % (d0, d1)
    print("sound 86box    %s  first %d writes %s; key-ons per channel (ours/86Box) %s; %d channels identical (need %d)%s" % (
        "PASS" if ok else "FAIL", n, "identical" if prefix_ok else "DIFFER",
        " ".join("%d:%d/%d%s" % (c, x, y, "" if s else "~") for c, x, y, s, _ in rows if x or y),
        identical, MIN_IDENTICAL_CHANNELS, drift))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
