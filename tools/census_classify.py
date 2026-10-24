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
                entries no route runs; it stays in the denominator, except the byte ranges of
                docs/census-dead-ranges.tsv (slots no program binds or names), which count as dead.
  unreached     no walk reaches it and nothing marks it as data: possibly code only a computed jump
                reaches, possibly data nobody has looked at. It stays in the denominator until a person
                has read it and decided.
  data          strong evidence only: a zero fill of eight or more bytes (executing eight `add [bx+si],al`
                in a row is not code), or a printable-text run of eight or more that no walk decodes as
                code. There is no instruction to match.
  original      inside a row of docs/census-original.tsv: code that stays original by design (the STI-first
                timer interrupts, the never-returning C start-up, the palette loaders that run an STI
                inside their loop, the divide-error handler no route reaches)
  dead          never-run code of a row of docs/census-dead.tsv: certified unreachable in the shipped game (no
                direct, pointer or computed reference, not an installed vector, every caller dead or a guarded
                arm whose guard cannot hold). Error paths are never listed; each row has a certificate

The historical census share is credited / (bytes - data - original - dead). Phase 2 also includes the separately
reported supplement of executed instructions outside these rows in all 17 game code files; add its named
bytes to the numerator and all its bytes to the denominator. Reviewed static code from docs/census-code.tsv
also adds workload; unreviewed discoveries remain a scope backlog. The floating LZEXE decompressor is excluded.
`data`, `original` and `dead` leave only uncredited bytes; a body that
does run some of those bytes keeps its credit. Linear census addresses are the image offsets of the loaded
programs, which is how the rest of the tooling reads them.

The rule that protects the score: when in doubt a byte stays in the denominator. Only `data` (strong evidence
above), `original` and `dead` (tables a person maintains, `dead` with a certificate per row) leave it, and a byte that a matched body does run is
credited whatever it was judged to be. docs/census-data.tsv records every data range a person read.

Since 21 Oct 2026 the P2 'Matched functions replacing translations' item uses the gameplay score instead (gameplay()
below): only bytes a validated route executes, matched bytes a route runs, and design-rule original code with a
reviewed entry in docs/reference-reviewed.tsv. The classes above remain the previous all-code measure, and the
gameplay score's checklist counts every byte of that measure's scope that no route executes.
"""
import argparse
import collections
import glob
import hashlib
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, "recompiler"))
import progress  # noqa: E402

ORIGINAL = os.path.join(ROOT, "docs", "census-original.tsv")
DEAD = os.path.join(ROOT, "docs", "census-dead.tsv")
DEAD_RANGES = os.path.join(ROOT, "docs", "census-dead-ranges.tsv")
DATA_TABLE = os.path.join(ROOT, "docs", "census-data.tsv")
CODE_TABLE = os.path.join(ROOT, "docs", "census-code.tsv")
REFERENCE_REVIEWED = os.path.join(ROOT, "docs", "reference-reviewed.tsv")
CLASSES = ("credited", "ran", "never_run", "unreached", "slots", "data", "original", "dead")
REVIEW_STATUSES = ("stays-original", "machine-option-only", "dead-certified", "probable-dead", "needs-route", "data",
                   "unreached-held")
# The design rules under which a routine stays original (owner, 21 Oct 2026; program launcher and hardware wait loop added 23 Oct 2026). A routine declined for any other
# reason (unequal, waits the routes cannot check, main(), an interior arm) is remaining work.
DESIGN_RULES = ("STI-first handler", "C start-up", "palette loader with STI", "program launcher", "hardware wait loop")
SLOT = b"\xea\x00\x00\x00\x00"   # a far jump to 0000:0000: a blank slot the run time patches


def read_original(path=ORIGINAL):
    rows = set()
    with open(path, encoding="utf-8") as source:
        for line in source:
            if line.strip() and not line.startswith("#"):
                a = line.split("\t")
                rows.add((a[0].upper().replace(".EXE", ""), int(a[1], 16)))
    return rows


def read_dead(path=DEAD):
    """Rows (module, linear) whose never-run code is certified unreachable in the shipped game (docs/census-dead.tsv)."""
    return read_original(path)


def read_dead_ranges(path=DEAD_RANGES):
    """{module: set of linear addresses} for byte ranges certified unreachable that are not census rows: jump slots no
    program binds or names, and code bytes no walk decodes (docs/census-dead-ranges.tsv)."""
    out = collections.defaultdict(set)
    if os.path.exists(path):
        with open(path, encoding="utf-8") as source:
            for line in source:
                if line.strip() and not line.startswith("#"):
                    a = line.split("	")
                    start = int(a[1], 16)
                    out[a[0].upper().replace(".EXE", "")].update(range(start, start + int(a[2])))
    return out


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


def reviewed_code_bytes(mods, path=CODE_TABLE):
    """Read human-reviewed static code ranges, pinned by hash and instruction boundaries.

    This validates a review, not a guess that decodable bytes must be code.
    The table records the code's purpose and the independent review evidence.
    A stale hash, partial instruction, wrong count or overlapping review fails
    the refresh rather than silently changing the denominator.
    """
    from x86dec import DecodeError, K_INVALID, decode
    out = collections.defaultdict(set)
    with open(path, encoding="utf-8") as source:
        for number, line in enumerate(source, 1):
            if not line.strip() or line.startswith("#"):
                continue
            fields = line.rstrip("\n").split("\t")
            if len(fields) != 7 or not fields[6].strip():
                raise ValueError("%s:%d: expected module, segment, IP, length, count, SHA256, evidence" % (path, number))
            name, seg, ip, size, count, digest, _evidence = fields
            seg, ip, size, count = int(seg, 16), int(ip, 16), int(size), int(count)
            mod = mods.get(name)
            if mod is None or not (0 <= seg <= 0xFFFF and 0 <= ip < ip + size <= 0x10000 and count > 0):
                raise ValueError("%s:%d: invalid module or range" % (path, number))
            off = mod.off(seg, ip)
            if off < 0 or off + size > len(mod.image):
                raise ValueError("%s:%d: range outside image" % (path, number))
            if hashlib.sha256(mod.image[off:off + size]).hexdigest() != digest:
                raise ValueError("%s:%d: reviewed code hash differs" % (path, number))
            pos, found = ip, 0
            try:
                while pos < ip + size:
                    ins = decode(mod.image, mod.off(seg, pos), pos)
                    if ins.kind == K_INVALID or ins.length <= 0 or pos + ins.length > ip + size:
                        raise ValueError("invalid or partial instruction")
                    pos += ins.length
                    found += 1
            except (DecodeError, ValueError) as error:
                raise ValueError("%s:%d: reviewed range does not decode cleanly: %s" % (path, number, error)) from error
            if found != count:
                raise ValueError("%s:%d: reviewed instruction count differs" % (path, number))
            span = set(range(seg * 16 + ip, seg * 16 + ip + size))
            if span & out[name]:
                raise ValueError("%s:%d: overlapping code reviews" % (path, number))
            out[name].update(span)
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


def classify(rows, covered, ran, mods, original, reviewed=None, dead=(), dead_ranges=None, scope_bytes=None):
    """rows: [(module, linear, size)]. Returns (totals, per_row) with per_row[i] = Counter of class -> bytes.
    scope_bytes, when given, collects {module: {linear: class}} for the distinct bytes the old measure keeps in its
    denominator (every class but data, original and dead; a byte two rows share is kept once, with its first class)."""
    code = code_bytes(rows, mods, ran)
    filler = {m: filler_bytes(mods[m].image) for m in mods}
    slots = {m: slot_bytes(mods[m].image) for m in mods}
    reviewed = reviewed or {}
    dead_ranges = dead_ranges or {}
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
                kind = "dead" if b in dead_ranges.get(module, ()) else "slots"
            elif b in cd and (module, linear) in dead:
                kind = "dead"
            elif b in cd:
                kind = "never_run"
            elif b in dead_ranges.get(module, ()):
                kind = "dead"
            else:
                kind = "unreached"
            row[kind] += 1
            if scope_bytes is not None and kind not in ("data", "original", "dead"):
                scope_bytes.setdefault(module, {}).setdefault(b, kind)
        per_row.append(row)
        totals.update(row)
    return totals, per_row


def load_rows(path):
    with open(path, encoding="utf-8") as source:
        lines = [l.rstrip("\n").split("\t") for l in source][1:]
    return [(r[0], int(r[1], 16), int(r[2])) for r in lines if len(r) > 2]


def modules_scope():
    """All shipped game code files; the shared floating decompressor is not game code."""
    from modules import CODE_FILES
    return {progress.module_key(name) for name in CODE_FILES}


def supplemental_scope(rows, covered, ran, reviewed=None, scope_bytes=None):
    """Executed and reviewed code outside the old census, across all game code files.

    Keep the historical rows intact. Add each outside byte once if a validated
    interpreter capture or a matched body's proof executed its instruction.
    Human-reviewed static code is added from the separately checked table;
    static discovery alone adds nothing here.
    Keep zero-byte modules visible, including NSOUND.117 until execution or
    a static review supplies its workload. Unknown modules and the floating
    decompressor remain separately reported and never enter the score.
    scope_bytes, when given, also collects each outside byte as supp-credited, supp-ran or supp-reviewed-static.
    """
    inside = collections.defaultdict(set)
    reviewed = reviewed or {}
    for module, linear, size in rows:
        inside[module].update(range(linear, linear + size))
    by_module = {}
    included = modules_scope() | set(inside)
    for module in sorted(included):
        census = inside.get(module, set())
        named = covered.get(module, set())
        executed = ran.get(module, set()) | named
        extra = (executed | reviewed.get(module, set())) - census
        by_module[module] = dict(bytes=len(extra), credited=len(extra & named),
                                reviewed_static_bytes=len(extra - executed))
        if scope_bytes is not None:
            out = scope_bytes.setdefault(module, {})
            for b in extra:
                out[b] = ("supp-credited" if b in named else "supp-ran" if b in ran.get(module, ()) else
                          "supp-reviewed-static")
    other = {module: len(bytes_) for module, bytes_ in sorted(ran.items()) if module not in included}
    return dict(basis="census plus executed and reviewed static code in all 17 game code files",
                bytes=sum(v["bytes"] for v in by_module.values()),
                credited=sum(v["credited"] for v in by_module.values()),
                reviewed_static_bytes=sum(v["reviewed_static_bytes"] for v in by_module.values()),
                by_module=by_module, other_modules_executed_bytes=other)


def read_reference_reviewed(path=REFERENCE_REVIEWED, modules=None):
    """The reviewed reference entries (docs/reference-reviewed.tsv): one status per byte range.

    Columns: module, linear start (hex), bytes, name, purpose, status, why not matched, reviewer note. Every
    text column must be filled (the reviewer note is the second reader's record), the status must be one of
    REVIEW_STATUSES, the module a game code file, and no two entries may share a byte: a malformed table
    stops the refresh rather than silently changing the score."""
    modules = modules_scope() if modules is None else set(modules)
    entries, spans = [], collections.defaultdict(set)
    with open(path, encoding="utf-8") as source:
        for number, line in enumerate(source, 1):
            if not line.strip() or line.startswith("#"):
                continue
            fields = line.rstrip("\n").split("\t")
            if len(fields) != 8 or not all(f.strip() for f in fields):
                raise ValueError("%s:%d: expected module, linear start, bytes, name, purpose, status, why not matched, "
                                 "reviewer note, all filled" % (path, number))
            module, start, size, name, purpose, status, why, note = (f.strip() for f in fields)
            module = progress.module_key(module)
            try:
                start, size = int(start, 16), int(size)
            except ValueError as error:
                raise ValueError("%s:%d: linear start must be hex and bytes decimal" % (path, number)) from error
            if module not in modules or start < 0 or size <= 0:
                raise ValueError("%s:%d: unknown module or empty range" % (path, number))
            if status not in REVIEW_STATUSES:
                raise ValueError("%s:%d: status must be exactly one of %s" % (path, number, ", ".join(REVIEW_STATUSES)))
            span = set(range(start, start + size))
            if span & spans[module]:
                raise ValueError("%s:%d: overlaps another reviewed entry (one status per range)" % (path, number))
            spans[module] |= span
            entries.append(dict(module=module, start=start, bytes=size, name=name, purpose=purpose, status=status,
                                why=why, note=note))
    return entries


def original_bytes(rows, original):
    """{module: linear addresses} of the census rows docs/census-original.tsv lists (the design-rule rows)."""
    out = collections.defaultdict(set)
    for module, linear, size in rows:
        if (module, linear) in original:
            out[module].update(range(linear, linear + size))
    return out


def check_design_rule(entries, rows, original):
    """A stays-original entry counts only for a routine that stays original by design rule (owner, 21 Oct 2026).

    Inside the census, every byte of the entry must lie in a docs/census-original.tsv row: a declined routine
    (VGAME main, MPS_LOGO 1ADC/1C82, START 8427, END 4393, END 57B0, ...) is remaining work, not original code.
    Every entry, inside or outside the census, must name its design rule at the start of `why not matched`: a
    census-original.tsv row can be wider than the design-rule routine (a C start-up row also holds the run time's
    divide-error entry), so the row alone is not evidence (second reader, 21 Oct 2026)."""
    inside = collections.defaultdict(set)
    for module, linear, size in rows:
        inside[module].update(range(linear, linear + size))
    orig = original_bytes(rows, original)
    for e in entries:
        if e["status"] != "stays-original":
            continue
        span = set(range(e["start"], e["start"] + e["bytes"]))
        census = span & inside[e["module"]]
        if census - orig[e["module"]]:
            raise ValueError("stays-original entry %s %05X covers census bytes outside docs/census-original.tsv"
                             % (e["module"], e["start"]))
        if not e["why"].lower().startswith(tuple(r.lower() for r in DESIGN_RULES)):
            raise ValueError("stays-original entry %s %05X must start `why not matched` with a "
                             "design rule (%s)" % (e["module"], e["start"], "; ".join(DESIGN_RULES)))


def gameplay(covered, ran, scope_bytes, original, entries, modules):
    """The gameplay score (owner, 21 Oct 2026), from byte sets by module and linear address.

    denominator D  every byte of an instruction a validated route capture executed, in the game code files
    numerator   a  matched-body bytes (named cover) that a route executed: fixture-only coverage does not count
                b  executed bytes of a reviewed stays-original entry (design-rule routines only, check_design_rule)
                   that a is not already crediting
    remaining      D - a - b; of it, `original_awaiting_review` is design-rule original code (census-original.tsv
                   rows) that has no reviewed entry yet
    checklist      every byte of the previous all-code measure's scope (scope_bytes) that is not in D, split by
                   whether a reviewed entry (any status) covers it
    """
    ref_any, ref_orig = collections.defaultdict(set), collections.defaultdict(set)
    for e in entries:
        span = range(e["start"], e["start"] + e["bytes"])
        ref_any[e["module"]].update(span)
        if e["status"] == "stays-original":
            ref_orig[e["module"]].update(span)
    unknown = set(scope_bytes) - set(modules)
    assert not unknown, "previous scope has bytes in modules outside the game code files: %s" % sorted(unknown)
    by_module, classes = {}, collections.defaultdict(collections.Counter)
    t = collections.Counter()
    for m in sorted(modules):
        d = set(ran.get(m, ()))
        cov = covered.get(m, set())
        a = cov & d
        b = (d & ref_orig[m]) - a
        r = d - a - b
        waiting = (d & original.get(m, set())) - a - b
        assert not (a & b) and not (r & (a | b)) and a | b | r == d and len(a) + len(b) + len(r) == len(d)
        s = scope_bytes.get(m, {})
        out = [x for x in s if x not in d]
        with_ref = sum(1 for x in out if x in ref_any[m])
        for x in out:
            classes[s[x]]["with_reviewed_entry" if x in ref_any[m] else "without_reviewed_entry"] += 1
        row = dict(denominator=len(d), matched_route_run=len(a), stays_original_reviewed=len(b),
                   fixture_only=len(cov - d), remaining=len(r), original_awaiting_review=len(waiting),
                   not_in_denominator=len(out), not_in_denominator_reviewed=with_ref,
                   in_previous_scope=sum(1 for x in d if x in s))
        by_module[m] = row
        t.update(row)
    score = (t["matched_route_run"] + t["stays_original_reviewed"]) / t["denominator"] if t["denominator"] else 0.0
    return dict(
        basis="every instruction byte a validated route capture executes in the 17 game code files",
        denominator=t["denominator"], matched_route_run=t["matched_route_run"],
        stays_original_reviewed=t["stays_original_reviewed"], score=score,
        fixture_only=t["fixture_only"], remaining=t["remaining"], original_awaiting_review=t["original_awaiting_review"],
        reviewed_entries=len(entries),
        checklist=dict(bytes=t["not_in_denominator"], with_reviewed_entry=t["not_in_denominator_reviewed"],
                       without_reviewed_entry=t["not_in_denominator"] - t["not_in_denominator_reviewed"],
                       by_class={k: dict(with_reviewed_entry=v["with_reviewed_entry"],
                                         without_reviewed_entry=v["without_reviewed_entry"])
                                 for k, v in sorted(classes.items())}),
        crosscheck=dict(previous_scope_distinct=sum(len(v) for v in scope_bytes.values()),
                        denominator_in_previous_scope=t["in_previous_scope"],
                        denominator_outside_previous_scope=t["denominator"] - t["in_previous_scope"]),
        by_module=by_module)


def run(census, cover_dir, route_dir, data_dir, scope=None, detail=None):
    """detail, when given, receives what the gameplay score needs: rows, covered, ran and the previous scope's
    distinct bytes (scope must be given too)."""
    from modules import load_all
    mods = {m.name.upper().replace(".EXE", ""): m for m in load_all(data_dir)}
    rows = load_rows(census)
    covered = progress.named_bytes(cover_dir, data_dir, route_dir)
    audit = {}
    ran = route_instructions(route_dir, mods, audit)
    scope_bytes = {} if detail is not None else None
    totals, per_row = classify(rows, covered, ran, mods, read_original(), read_data_table(), dead=read_dead(),
                               dead_ranges=read_dead_ranges(), scope_bytes=scope_bytes)
    if scope is not None:
        scope.update(supplemental_scope(rows, covered, ran, reviewed_code_bytes(mods), scope_bytes=scope_bytes))
        scope["route_records"] = audit
    if detail is not None:
        detail.update(rows=rows, covered=covered, ran=ran, scope_bytes=scope_bytes)
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
    creditable = total - totals["data"] - totals["original"] - totals["dead"]
    print("old census creditable denominator %d; named-byte share %.2f%% (whole census %.2f%%)"
          % (creditable, 100.0 * totals["credited"] / creditable, 100.0 * totals["credited"] / total))
    print("supplement outside census: %d bytes, %d credited; expanded game-code share %.2f%%"
          % (scope["bytes"], scope["credited"],
             100.0 * (totals["credited"] + scope["credited"]) / (creditable + scope["bytes"])))
    for module, counts in scope["by_module"].items():
        print("  %-9s %5d extra, %4d credited" % (module, counts["bytes"], counts["credited"]))
    print("route records (unique): %s" % scope["route_records"])
    print("other modules, executed bytes outside score: %s" % scope["other_modules_executed_bytes"])
    if a.rows:
        left = sorted(((r["ran"] + r["never_run"], row, r) for row, r in zip(rows, per_row)), key=lambda x: -x[0])
        for n, (module, linear, size), r in left[:a.rows]:
            print("%-9s 0x%05X %5d  ran %4d  never_run %4d  unreached %4d  slots %4d  data %4d  original %4d  dead %4d" %
                  (module, linear, size, r["ran"], r["never_run"], r["unreached"], r["slots"], r["data"], r["original"], r["dead"]))


if __name__ == "__main__":
    main()
