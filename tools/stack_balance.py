#!/usr/bin/env python3
"""stack_balance.py - find paths that leave a routine with the stack moved.

    py tools/stack_balance.py --gen DIR --census FILE [--module VGAME]

Reads the instruction listing the recompiler writes into the generated C
(`/* SEG:OFF  bytes  text */` comments) and walks every path of each census
function from its entry, tracking SP against the entry. It reports a RET or
RETF reached at a depth other than 0. Near calls are taken to return, popping
what the callee's first RET pops (`ret N`); MOV SP,... and LEAVE restore a
frame and end the tracking on that path; indirect jumps are not followed.
Merges at different depths are not reported: compiled C defers argument
clean-up to its frame restore. Used for docs/bugs.md D10 (9 Oct 2026).
"""
import argparse
import glob
import os
import re

LINE = re.compile(r"/\* ([0-9A-F]{4}):([0-9A-F]{4})  ([0-9a-f]+)\s+(.*?) \*/")


def listing(gen, module):
    ins = {}
    for path in glob.glob(os.path.join(gen, "gen_%s_*.c" % module.replace(".", "_"))):
        for m in LINE.finditer(open(path, encoding="utf-8", errors="replace").read()):
            ins[(m.group(1), int(m.group(2), 16))] = (len(m.group(3)) // 2, m.group(4))
    return ins


def target(ops):
    m = re.match(r"(0x[0-9a-f]+)$", ops.strip())
    return int(m.group(1), 16) & 0xFFFF if m else None


def delta(mn, ops):
    step = {"push": -2, "pop": 2, "pushf": -2, "popf": 2, "pusha": -16, "popa": 16}
    if mn in step:
        return step[mn]
    m = re.match(r"sp, (0x[0-9a-f]+|\d+)$", ops)
    if m and mn in ("add", "sub"):
        v = int(m.group(1), 0)
        return v if mn == "add" else -v
    return 0


def callee_pops(ins, seg, t, cache):
    if (seg, t) not in cache:
        ip, n, pops = t, 0, 0
        while (seg, ip) in ins and n < 400:
            size, text = ins[(seg, ip)]
            parts = text.split(None, 1)
            if parts[0] in ("ret", "retf"):
                pops = int(parts[1], 0) if len(parts) > 1 else 0
                break
            ip, n = (ip + size) & 0xFFFF, n + 1
        cache[(seg, t)] = pops
    return cache[(seg, t)]


def check(ins, seg, entry, cache):
    seen, problems, todo = set(), [], [(entry, 0)]
    while todo:
        ip, d = todo.pop()
        while (seg, ip) not in seen:
            seen.add((seg, ip))
            if (seg, ip) not in ins:
                break
            size, text = ins[(seg, ip)]
            parts = text.split(None, 1)
            mn, ops = parts[0], parts[1] if len(parts) > 1 else ""
            nxt = (ip + size) & 0xFFFF
            if mn == "leave" or (mn == "mov" and ops.startswith("sp, ")):
                d = None
            elif d is not None:
                d += delta(mn, ops)
                if mn == "call" and target(ops) is not None:
                    d += callee_pops(ins, seg, target(ops), cache)
            if mn in ("ret", "retf", "iret"):
                if d not in (0, None):
                    problems.append("%04X %s at depth %d" % (ip, text, d))
                break
            if mn == "jmp":
                ip = target(ops)
                if ip is None:
                    break
                continue
            if mn.startswith("j") or mn.startswith("loop"):
                if target(ops) is not None:
                    todo.append((target(ops), d))
            elif mn == "ljmp":
                break
            ip = nxt
    return problems


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--gen", required=True, help="the generated C directory")
    ap.add_argument("--census", required=True, help="tools/reimp_names.py output")
    ap.add_argument("--module", default="VGAME")
    a = ap.parse_args()
    ins = listing(a.gen, a.module + ".EXE")
    cache, checked, flagged = {}, 0, 0
    for line in open(a.census, encoding="utf-8"):
        f = line.rstrip("\n").split("\t")
        if len(f) < 5 or f[0] != a.module or not f[3].startswith("R_"):
            continue
        seg, off = f[3].split("_")[-2:]
        checked += 1
        problems = check(ins, seg, int(off, 16), cache)
        if problems:
            flagged += 1
            print("%s %s:%s %s: %s" % (a.module, seg, off, f[4] or f[5] or "-", "; ".join(problems[:4])))
    print("%d functions walked, %d leave with the stack moved" % (checked, flagged))


if __name__ == "__main__":
    main()
