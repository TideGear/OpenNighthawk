#!/usr/bin/env python3
"""census_classify.py - what the census bytes are, and which of them Phase 2 can still credit.

    py tools/census_classify.py --census census.tsv --named-cover DIR --route-cover DIR --data GOG_DIR [--rows N]

Phase 2 is scored by census bytes that a matched body runs (tools/progress.py). Not every census byte can
ever earn credit, so the score's denominator is split. Every census byte is exactly one of:

  credited      inside an original instruction a matched body runs (the score's numerator)
  ran           inside an instruction some route ran, but no matched body covers it yet: workable now
  never_run     decodes as code reachable from a census entry or from a route-run instruction, but no
                route runs it (error paths, library arms the game never uses, code behind inputs no route
                supplies): workable only with a new route or a decision that it is unreachable
  slots         the entry-point jump vectors that each program's segment binder fills in at start-up (VGAME
                0FAE:0002, matched as vgame_bind_segment_entries, writes EA + offset + segment into 84
                five-byte slots; the same binder code is in SETUP, PLAYER, DSWAP, START and END, and exactly
                those programs have the table). In the file the unfilled slots read EA 00 00 00 00. They
                are live code: in VGAME 47 of the 84 are executed by the routes. This class is the
                entries no route runs; it stays in the denominator.
  unreached     no walk reaches it and nothing marks it as data: possibly code only a computed jump
                reaches, possibly data nobody has looked at. It stays in the denominator until a person
                has read it and decided.
  data          strong evidence only: a zero fill of eight or more bytes (executing eight `add [bx+si],al`
                in a row is not code), or a printable-text run of eight or more that no walk decodes as
                code. There is no instruction to match.
  original      inside a row of docs/census-original.tsv: code that stays original by design (the STI-first
                timer interrupts, the never-returning C start-up, the palette loaders that run an STI
                inside their loop, the divide-error handler no route reaches)

The historical census share is credited / (bytes - data - original). Phase 2 also includes the separately
reported supplement of executed instructions outside these rows in the same seven programs; add its named
bytes to the numerator and all its bytes to the denominator. Static-only discoveries and other modules are
an explicit scope backlog. `data` and `original` leave only uncredited bytes; a body that
does run some of those bytes keeps its credit. Linear census addresses are the image offsets of the loaded
programs, which is how the rest of the tooling reads them.

The rule that protects the score: when in doubt a byte stays in the denominator. Only `data` (strong evidence
above) and `original` (a table a person maintains) leave it, and a byte that a matched body does run is
credited whatever it was judged to be. docs/census-data.tsv records every data range a person read.
"""
import argparse
import collections
import glob
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, "recompiler"))
import progress  # noqa: E402

ORIGINAL = os.path.join(ROOT, "docs", "census-original.tsv")
DATA_TABLE = os.path.join(ROOT, "docs", "census-data.tsv")
CLASSES = ("credited", "ran", "never_run", "unreached", "slots", "data", "original")
SLOT = b"\xea\x00\x00\x00\x00"   # a far jump to 0000:0000: a blank slot the run time patches


def read_original(path=ORIGINAL):
    rows = set()
    with open(path, encoding="utf-8") as source:
        for line in source:
            if line.strip() and not line.startswith("#"):
                a = line.split("\t")
                rows.add((a[0].upper().replace(".EXE", ""), int(a[1], 16)))
    return rows


def read_data_table(path=DATA_TABLE):
    """{module: set of linear addresses} for the ranges a person has read and judged to be data."""
    out = collections.defaultdict(set)
    with open(path, encoding="utf-8") as source:
        for line in source:
            if line.strip() and not line.startswith("#"):
                a = line.split("\t")
                start = int(a[1], 16)
                out[a[0].upper().replace(".EXE", "")].update(range(start, start + int(a[2])))
    return out


def slot_bytes(image, minimum=4):
    """Offsets inside runs of `minimum` or more consecutive unfilled entry vectors (EA 00 00 00 00)."""
    out, i, n = set(), 0, len(image)
    while i < n:
        j = i
        while image[j:j + 5] == SLOT:
            j += 5
        if (j - i) // 5 >= minimum:
            out.update(range(i, j))
        i = max(j, i + 1)
    return out


def segment_of(linear):
    return (linear >> 4) & 0xF000 if linear > 0xFFFF else 0


def route_instructions(route_dir, mods, audit=None):
    """Decode executed instructions, after checking module identity and the captured bytes.

    recomp.c records a FOUR-BYTE WINDOW, not the instruction's bytes or its length.
    Decode the image for the length; compare only the captured part of that instruction
    (the window can extend into the next instruction). Ignore relocated operands, as
    the translator does. Reject the unpacker's overlapping addresses and other changed
    code. The one supported patch is a bound entry vector: EA remains EA and the four
    operand bytes are filled by the segment binder. Addresses remain module-relative
    CS:IP until after validation; COM's origin affects image indexing, not the result.

    audit counts unique records, since the same instruction occurs in many routes.
    Rejected records are reported, never treated as executed image instructions.
    """
    from x86dec import DecodeError, K_INVALID, decode
    paths = sorted(glob.glob(os.path.join(route_dir, "*.cov")))
    if not paths:
        raise ValueError("no route coverage files in %s" % route_dir)
    records = set()
    for path in paths:
        with open(path, encoding="utf-8") as source:
            for number, line in enumerate(source, 1):
                if not line.strip() or line.startswith("#"):
                    continue
                a = line.split()
                if len(a) != 5:
                    raise ValueError("%s:%d: expected NAME HASH SEG IP WINDOW" % (path, number))
                name = a[0].upper().replace(".EXE", "")
                record = (name, int(a[1], 16), int(a[2], 16), int(a[3], 16), bytes.fromhex(a[4]))
                if len(record[4]) != 4 or not all(0 <= v <= 0xFFFF for v in record[2:4]):
                    raise ValueError("%s:%d: invalid coverage address or four-byte window" % (path, number))
                records.add(record)
    if not records:
        raise ValueError("route coverage contains no instruction records in %s" % route_dir)
    ran, counts = collections.defaultdict(set), collections.Counter()
    reloc = {name: mod.reloc_bytes() for name, mod in mods.items()}
    slots = {name: slot_bytes(mod.image) for name, mod in mods.items()}
    for name, file_hash, seg, ip, raw in sorted(records):
        mod = mods.get(name)
        if mod is None or mod.file_hash != file_hash:
            counts["identity"] += 1
            continue
        off = mod.off(seg, ip)
        if off < 0 or off >= len(mod.image):
            counts["outside"] += 1
            continue
        try:
            ins = decode(mod.image, off, ip)
        except DecodeError:
            counts["decode"] += 1
            continue
        if ins.kind == K_INVALID or off + ins.length > len(mod.image):
            counts["decode"] += 1
            continue
        bound_slot = (mod.image[off:off + 5] == SLOT and off in slots[name] and raw[0] == 0xEA)
        if not bound_slot and any(mod.image[off + k] != raw[k] for k in range(min(4, ins.length))
                                  if off + k not in reloc[name]):
            counts["changed"] += 1
            continue
        ran[name].update(range(seg * 16 + ip, seg * 16 + ip + ins.length))
        counts["accepted"] += 1
        if bound_slot:
            counts["bound_slots"] += 1
    if audit is not None:
        audit.update(counts)
    return ran


def code_bytes(rows, mods, ran):
    """Bytes decoded by a walk from every census entry, plus every byte a route ran."""
    from x86dec import decode
    code = collections.defaultdict(set)
    for module, linear, _size in rows:
        mod = mods[module]
        seg = segment_of(linear)
        try:
            walked = progress.own_walk(lambda m, s, i: decode(mod.image, mod.off(s, i), i), module, seg,
                                       linear - seg * 16, set())
        except Exception:   # an entry that runs off the image or cannot start: data, as far as a walk can tell
            continue
        for ip, ins in walked.items():
            code[module].update(range(seg * 16 + ip, seg * 16 + ip + ins.length))
    for module, bytes_ in ran.items():
        code[module] |= bytes_
    return code


def filler_bytes(image):
    """(zero, text): offsets inside zero runs of 8 or more and printable-text runs of 8 or more."""
    zero, text, i, n = set(), set(), 0, len(image)
    while i < n:
        j = i
        if image[i] == 0:
            while j < n and image[j] == 0:
                j += 1
            if j - i >= 8:
                zero.update(range(i, j))
        elif 0x20 <= image[i] < 0x7F:
            while j < n and 0x20 <= image[j] < 0x7F:
                j += 1
            # Opcodes are printable too (cmp al,'s' / je / cmp al,'n' reads "<st'<nt"), so a run is text only
            # when it is mostly letters and spaces, which a run of opcodes and displacements is not.
            letters = sum(1 for k in range(i, j) if image[k] == 0x20 or chr(image[k]).isalpha())
            if j - i >= 8 and letters * 100 >= 80 * (j - i):
                text.update(range(i, j))
        i = max(j, i + 1)
    return zero, text


def classify(rows, covered, ran, mods, original, reviewed=None):
    """rows: [(module, linear, size)]. Returns (totals, per_row) with per_row[i] = Counter of class -> bytes."""
    code = code_bytes(rows, mods, ran)
    filler = {m: filler_bytes(mods[m].image) for m in mods}
    slots = {m: slot_bytes(mods[m].image) for m in mods}
    reviewed = reviewed or {}
    totals, per_row = collections.Counter(), []
    for module, linear, size in rows:
        row = collections.Counter()
        cov, run, cd = covered.get(module, ()), ran.get(module, ()), code.get(module, ())
        zero, text, slot, seen = filler[module][0], filler[module][1], slots[module], reviewed.get(module, ())
        for b in range(linear, linear + size):
            if b in cov:
                kind = "credited"
            elif (module, linear) in original:
                kind = "original"
            elif b in run:
                kind = "ran"
            elif b in seen or b in zero or (b in text and b not in cd):
                kind = "data"
            elif b in slot:
                kind = "slots"
            elif b in cd:
                kind = "never_run"
            else:
                kind = "unreached"
            row[kind] += 1
        per_row.append(row)
        totals.update(row)
    return totals, per_row


def load_rows(path):
    with open(path, encoding="utf-8") as source:
        lines = [l.rstrip("\n").split("\t") for l in source][1:]
    return [(r[0], int(r[1], 16), int(r[2])) for r in lines if len(r) > 2]


def supplemental_scope(rows, covered, ran):
    """Executed code outside the old census, in its seven programs only.

    Keep the historical rows intact. Add each outside byte once if a validated
    interpreter capture or a matched body's proof executed its instruction.
    Static discovery alone is not execution evidence and adds nothing here.
    The other modules remain separately reported debt, not silently included
    in a measure whose matched table only supports these seven programs.
    """
    inside = collections.defaultdict(set)
    for module, linear, size in rows:
        inside[module].update(range(linear, linear + size))
    by_module = {}
    for module, census in sorted(inside.items()):
        named = covered.get(module, set())
        extra = (ran.get(module, set()) | named) - census
        by_module[module] = dict(bytes=len(extra), credited=len(extra & named))
    other = {module: len(bytes_) for module, bytes_ in sorted(ran.items()) if module not in inside}
    return dict(basis="census plus executed instructions in the seven census programs",
                bytes=sum(v["bytes"] for v in by_module.values()),
                credited=sum(v["credited"] for v in by_module.values()),
                by_module=by_module, other_modules_executed_bytes=other)


def run(census, cover_dir, route_dir, data_dir, scope=None):
    from modules import load_all
    mods = {m.name.upper().replace(".EXE", ""): m for m in load_all(data_dir)}
    rows = load_rows(census)
    covered = progress.named_bytes(cover_dir, data_dir, route_dir)
    audit = {}
    ran = route_instructions(route_dir, mods, audit)
    totals, per_row = classify(rows, covered, ran, mods, read_original(), read_data_table())
    if scope is not None:
        scope.update(supplemental_scope(rows, covered, ran))
        scope["route_records"] = audit
    return rows, totals, per_row


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--census", required=True)
    ap.add_argument("--named-cover", required=True)
    ap.add_argument("--route-cover", required=True)
    ap.add_argument("--data", required=True)
    ap.add_argument("--rows", type=int, default=0, help="also list the N rows with the most uncredited bytes")
    a = ap.parse_args()
    scope = {}
    rows, totals, per_row = run(a.census, a.named_cover, a.route_cover, a.data, scope=scope)
    total = sum(totals.values())
    for k in CLASSES:
        print("%-9s %7d  %5.2f%%" % (k, totals[k], 100.0 * totals[k] / total))
    creditable = total - totals["data"] - totals["original"]
    print("old census creditable denominator %d; named-byte share %.2f%% (whole census %.2f%%)"
          % (creditable, 100.0 * totals["credited"] / creditable, 100.0 * totals["credited"] / total))
    print("supplement outside census: %d bytes, %d credited; expanded seven-program share %.2f%%"
          % (scope["bytes"], scope["credited"],
             100.0 * (totals["credited"] + scope["credited"]) / (creditable + scope["bytes"])))
    for module, counts in scope["by_module"].items():
        print("  %-9s %5d extra, %4d credited" % (module, counts["bytes"], counts["credited"]))
    print("route records (unique): %s" % scope["route_records"])
    print("other modules, executed bytes outside score: %s" % scope["other_modules_executed_bytes"])
    if a.rows:
        left = sorted(((r["ran"] + r["never_run"], row, r) for row, r in zip(rows, per_row)), key=lambda x: -x[0])
        for n, (module, linear, size), r in left[:a.rows]:
            print("%-9s 0x%05X %5d  ran %4d  never_run %4d  unreached %4d  slots %4d  data %4d  original %4d" %
                  (module, linear, size, r["ran"], r["never_run"], r["unreached"], r["slots"], r["data"], r["original"]))


if __name__ == "__main__":
    main()
