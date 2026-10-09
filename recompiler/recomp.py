#!/usr/bin/env python3
"""recomp.py - translate the game's machine code to C.

    py recompiler/recomp.py --data INSTALL_DIR --out GEN_DIR [--coverage FILE ...]
                            [--only NAME ...] [--no-heuristics] [--no-comments]

Reads every code file of the user's own DOS installation, finds the code
(recompiler/discover.py), and writes one C function per region plus the
tables the run-time needs (src/recomp/recomp_gen.h) into GEN_DIR. Build with
`build.cmd -DF117R_GEN_DIR=GEN_DIR`.

Coverage files are written by the runtime (f117run --coverage FILE, or the
game with --coverage): every instruction the interpreter executed inside a
known module, as `NAME HASH SEG IP BYTES`. They seed regions static
discovery cannot reach (code reached only through function pointers).

THE OUTPUT IS DERIVED FROM THE ORIGINAL EXECUTABLES. It belongs in the
build tree or local/, never in the repository.
"""
from __future__ import annotations

import argparse
import os
import re
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

from discover import Discovery, Region  # noqa: E402
from emit import Ctx, emit              # noqa: E402
from modules import load_all            # noqa: E402

REGIONS_PER_FILE = 120
MAX_REGION_INSNS = 1024


def bounded_regions(regions, limit=MAX_REGION_INSNS):
    """Bound the compiler's CFG size; cross-piece flow uses the dispatcher.

    Every instruction keeps its decode and live operands. CHECK already
    polls at every instruction, so an extra dispatcher boundary changes no
    guest instruction or event deadline.
    """
    if limit < 1:
        raise ValueError("region limit must be positive")
    out = []
    for r in regions:
        if len(r.insns) <= limit:
            out.append(r)
            continue
        ips = sorted(r.insns)
        for start in range(0, len(ips), limit):
            part = ips[start:start + limit]
            seed = r.seed_ip if r.seed_ip in part else part[0]
            out.append(Region(r.seg, seed, {ip: r.insns[ip] for ip in part},
                              {ip: r.live[ip] for ip in part}))
    return out

try:
    import capstone
    _CS = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_16)
except Exception:          # comments are optional
    _CS = None


def tag(name):
    return re.sub(r"[^A-Za-z0-9]", "_", name)


def disasm_text(ins):
    if not _CS:
        return ""
    for i in _CS.disasm(ins.raw, ins.ip):
        t = ("%s %s" % (i.mnemonic, i.op_str)).strip()
        return t.replace("*/", "* /")
    return "?"


def read_coverage(paths, mods):
    by_key = {(m.name, m.file_hash): m for m in mods}
    seeds = {m.name: [] for m in mods}
    rejected = 0
    for p in paths:
        if not os.path.exists(p):
            print("coverage file %s does not exist" % p)
            continue
        for line in open(p):
            parts = line.split()
            if len(parts) < 4 or parts[0].startswith("#"):
                continue
            name, h, seg, ip = parts[0].upper(), int(parts[1], 16), int(parts[2], 16), int(parts[3], 16)
            raw = bytes.fromhex(parts[4]) if len(parts) > 4 else b""
            m = by_key.get((name, h))
            if not m:
                continue
            # The bytes executed must be this image's bytes (allowing for
            # relocation): a packed program's decompressor runs over the
            # same addresses before the image exists.
            off = m.off(seg, ip)
            if off < 0 or off + len(raw) > len(m.image):
                rejected += 1
                continue
            rb = m.reloc_bytes()
            if any(m.image[off + k] != raw[k] for k in range(len(raw)) if off + k not in rb):
                rejected += 1
                continue
            seeds[name].append((seg, ip))
    return seeds, rejected


MUTATIONS = {
    # A deliberate defect after one instruction's semantics, for proving the
    # parity check can see one (tools/mutation_check.py).
    "cf": "c->flags ^= F_CF;",
    "zf": "c->flags ^= F_ZF;",
    "ax": "c->r[R_AX] ^= 1;",
    "skip": None,   # the instruction does nothing (see mutate_lines)
}


def mutate_lines(lines, kind, ins):
    """Insert the mutation before the instruction retires (its last IC).
    "skip" instead makes the instruction do nothing at all - no effect, no
    branch - and continue at the next one."""
    if kind == "skip":
        timing = [line for line in lines if line.startswith("TIMING(")]
        return [lines[0]] + timing + ["rc_mutant_hits++;", "IC(0x%02X); EXIT(0x%04X);" % (ins.op, ins.next_ip)]
    stmt = MUTATIONS[kind] + " rc_mutant_hits++;"
    for i in range(len(lines) - 1, -1, -1):
        if lines[i].startswith("IC("):
            return lines[:i] + [stmt] + lines[i:]
    return lines + [stmt]


def write_module(mod, regions, out_dir, comments, mutate=None):
    regions = bounded_regions(regions)
    t = tag(mod.name)
    mutated = 0
    files = []
    # Region code, in chunks so the build compiles in parallel.
    entries = []
    runs = []
    region_rows = []
    for ri, r in enumerate(regions):
        r.cname = "R_%s_%04X_%04X" % (t, r.seg, r.seed_ip)
    chunk = 0
    for start in range(0, len(regions), REGIONS_PER_FILE):
        part = regions[start:start + REGIONS_PER_FILE]
        path = os.path.join(out_dir, "gen_%s_%03d.c" % (t, chunk))
        chunk += 1
        files.append(path)
        with open(path, "w", newline="\n") as f:
            f.write("/* GENERATED from %s by recompiler/recomp.py - derived from the original\n"
                    " * executable: never commit, never distribute. */\n" % mod.name)
            f.write('#include "recomp_gen.h"\n\n')
            f.write("#ifdef _MSC_VER\n#pragma warning(disable: 4102 4127 4189 4244 4310 4702)\n#endif\n\n")
            for r in part:
                ips = sorted(r.insns)
                f.write("int %s(cpu_t *c)\n{\n" % r.cname)
                f.write("    c->op_cs = c->seg[S_CS];\n    switch (c->ip) {\n")
                for ip in ips:
                    f.write("    case 0x%04X: goto L_%04X;\n" % (ip, ip))
                f.write("    default: return 0;\n    }\n")
                in_region = r.insns.__contains__
                for ip in ips:
                    ins = r.insns[ip]
                    if comments:
                        f.write("    /* %04X:%04X  %-14s %s */\n" % (
                            r.seg, ip, ins.raw.hex(), disasm_text(ins)))
                    lines = emit(ins, Ctx(in_region, frozenset(r.live[ip])))
                    if mutate and mutate[0] == mod.name and mutate[1] == mod.off(r.seg, ip):
                        lines = mutate_lines(lines, mutate[2], ins)
                        mutated += 1
                        f.write("    /* MUTANT: %s */\n" % mutate[2])
                    for line in lines:
                        f.write("    " + line + "\n")
                f.write("    return 1;\n}\n\n")
    # Tables.
    for ri, r in enumerate(regions):
        first = len(runs)
        spans = []
        for ip in sorted(r.insns):
            ins = r.insns[ip]
            off = mod.off(r.seg, ip)
            live = r.live[ip]
            for k in range(ins.length):
                if k not in live:
                    spans.append(off + k)
            entries.append((off, ri))
        spans = sorted(set(spans))
        cur = None
        for o in spans:
            if cur and o == cur[0] + cur[1]:
                cur[1] += 1
            else:
                if cur:
                    runs.append(tuple(cur))
                cur = [o, 1]
        if cur:
            runs.append(tuple(cur))
        region_rows.append((r.cname, r.seg, first, len(runs) - first))
    entries.sort()
    path = os.path.join(out_dir, "gen_%s_tab.c" % t)
    files.append(path)
    with open(path, "w", newline="\n") as f:
        f.write("/* GENERATED from %s - derived from the original executable: never commit. */\n" % mod.name)
        f.write('#include "recomp_gen.h"\n\n')
        for name, _, _, _ in region_rows:
            f.write("int %s(cpu_t *c);\n" % name)
        f.write("\nstatic const uint8_t IMAGE[%d] = {\n" % max(1, len(mod.image)))
        for i in range(0, len(mod.image), 24):
            f.write("  " + ",".join(str(b) for b in mod.image[i:i + 24]) + ",\n")
        f.write("};\n\nstatic const rc_run RUNS[%d] = {\n" % max(1, len(runs)))
        for o, n in runs:
            f.write("  {%d,%d},\n" % (o, n))
        f.write("};\n\nstatic const rc_region REGIONS[%d] = {\n" % max(1, len(region_rows)))
        for name, seg, first, n in region_rows:
            f.write("  {%s, 0x%04X, %d, %d},\n" % (name, seg, first, n))
        f.write("};\n\nstatic const rc_entry ENTRIES[%d] = {\n" % max(1, len(entries)))
        for off, ri in entries:
            f.write("  {%d,%d},\n" % (off, ri))
        f.write("};\n\n")
        f.write("const rc_module RCM_%s = {\n" % t)
        f.write('  "%s", 0x%016XULL, %d, %d, IMAGE,\n' % (mod.name, mod.file_hash, len(mod.image), mod.origin))
        f.write("  REGIONS, %d, ENTRIES, %d, RUNS, %d, %d,\n" % (
            len(region_rows), len(entries), len(runs), len(entries)))
        f.write("  %d, %d, %d\n};\n" % (1 if mod.kind == "floating" else 0,
                                       mod.probe[0] if mod.probe else 0, mod.probe[1] if mod.probe else 0))
    if mutate and mutate[0] == mod.name and not mutated:
        sys.exit("--mutate: no translated instruction starts at %s+%05X" % (mod.name, mutate[1]))
    return files


ROOT_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OVERRIDE_TABLES = (os.path.join(ROOT_DIR, "src", "fixes", "fixes.c"),
                   os.path.join(ROOT_DIR, "src", "matched", "matched.c"),
                   os.path.join(ROOT_DIR, "src", "matched", "observe.c"))


def override_sites(paths=OVERRIDE_TABLES):
    """The addresses the code overrides replace - the switchable fixes and
    the matched routines - read from their tables so the two cannot drift:
    {MODULE: [(seg, ip), ...]}."""
    if isinstance(paths, str):
        paths = (paths,)
    sites = {}
    for path in paths:
        try:
            with open(path, encoding="utf-8") as f:
                text = f.read()
        except OSError:
            continue
        for name, seg, ip in re.findall(
                r'\{\s*"[^"]+",\s*"([^"]+)",\s*[^,]+,\s*(0x[0-9A-Fa-f]+),\s*(0x[0-9A-Fa-f]+),\s*[A-Za-z_]\w*\s*,', text):
            sites.setdefault(name.upper(), []).append((int(seg, 16), int(ip, 16)))
    return sites


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--data", required=True, help="the game's install directory")
    ap.add_argument("--out", required=True, help="where the generated C goes")
    ap.add_argument("--coverage", action="append", default=[], help="coverage files from the runtime")
    ap.add_argument("--only", action="append", default=[], help="translate only these files")
    ap.add_argument("--no-heuristics", action="store_true")
    ap.add_argument("--no-comments", action="store_true")
    ap.add_argument("--mutate", help="NAME:IMAGE_OFFSET:KIND (cf, zf, ax) - plant a defect for mutation testing")
    a = ap.parse_args()
    mutate = None
    if a.mutate:
        name, off, kind = a.mutate.split(":")
        if kind not in MUTATIONS:
            sys.exit("--mutate kinds: %s" % ", ".join(MUTATIONS))
        mutate = (name.upper(), int(off, 16), kind)

    t0 = time.time()
    os.makedirs(a.out, exist_ok=True)
    for f in os.listdir(a.out):
        if f.startswith("gen_") and f.endswith(".c"):
            os.remove(os.path.join(a.out, f))
    print("reading modules from %s" % a.data)
    mods = load_all(a.data)
    if a.only:
        want = {n.upper() for n in a.only}
        mods = [m for m in mods if m.name in want]
    cov, rejected = read_coverage(a.coverage, mods)
    if a.coverage:
        print("coverage: %d seeds, %d rejected (bytes not this image's)" % (
            sum(len(v) for v in cov.values()), rejected))
    print("discovering code")
    sites = override_sites()
    if sites:
        print("override sites: " + ", ".join("%s %04X:%04X" % (n, s, i) for n, v in sorted(sites.items()) for s, i in v))
    total_insns = 0
    tags = []
    for m in mods:
        d = Discovery(m)
        regions = d.run(coverage=cov.get(m.name, []), heuristics=not a.no_heuristics,
                        isolate=sites.get(m.name, ()))
        total_insns += sum(len(r.insns) for r in regions)
        write_module(m, regions, a.out, not a.no_comments, mutate)
        tags.append(tag(m.name))
    with open(os.path.join(a.out, "gen_modules.c"), "w", newline="\n") as f:
        f.write("/* GENERATED - the module list. */\n#include \"recomp_gen.h\"\n\n")
        for t in tags:
            f.write("extern const rc_module RCM_%s;\n" % t)
        f.write("\nconst rc_module *const RC_MODULES[] = {\n")
        for t in tags:
            f.write("  &RCM_%s,\n" % t)
        f.write("};\nconst unsigned RC_NMODULES = %d;\n" % len(tags))
    print("%d instructions translated in %d modules in %.1f s -> %s" % (
        total_insns, len(mods), time.time() - t0, a.out))


if __name__ == "__main__":
    main()
