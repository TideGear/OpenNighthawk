#!/usr/bin/env python3
"""matched_draft.py - a starting point for a Phase 2 matched routine: the original's instructions as C.

    py tools/matched_draft.py --data GOG_DIR --module VGAME.EXE --ip 0xC6B3 [--seg 0] [--name vgame_dist]
                              [--out DIR] [--dossier]

Walks the routine from its entry (fall-through, jumps and conditional jumps; calls continue after the
call; RET, indirect jumps and far jumps end a path) and writes one function in the shape
src/matched/matched.c expects: each instruction as the translator emits it (recompiler/emit.py, the
interpreter's own semantics), a decline before each instruction when the run loop must look at events
(the entry declines, so the original runs; later ones stop with IP there, so the original finishes),
the clock per instruction, and near and far calls through guest_call. Verify the draft with
tests/func_lockstep.c, then rewrite it into named, explained code with lockstep green at every step.
--dossier writes the same walk's bytes, ranges, calls, frontier and identities beside the draft.

THE DRAFT IS DERIVED FROM THE ORIGINAL EXECUTABLE, like the translation: it is written to --out
(default D:/f117-gate/drafts), never into the repository. Only the rewritten routine - named, its
logic explained, not a transcript of instructions - goes into src/matched/matched.c.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(HERE), "recompiler"))
from emit import Ctx, emit                                     # noqa: E402
from modules import load_all                                    # noqa: E402
from x86dec import (decode, K_CALL, K_CALLFAR, K_CALLIND, K_HLT, K_INVALID, K_JCC, K_JMP, K_JMPFAR,  # noqa: E402
                    K_JMPIND, K_RET)

try:
    import capstone
    _CS = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_16)
except Exception:
    _CS = None


def text(ins):
    if not _CS:
        return ""
    for i in _CS.disasm(ins.raw, ins.ip):
        return ("%s %s" % (i.mnemonic, i.op_str)).strip().replace("*/", "* /")
    return "?"


def walk(mod, seg, entry, limit=4000):
    """Every instruction reachable from the entry without leaving through a return or a computed jump."""
    seen, todo = {}, [entry]
    while todo:
        ip = todo.pop()
        while ip not in seen:
            off = mod.off(seg, ip)
            if off < 0 or off >= len(mod.image) or len(seen) >= limit:
                break
            ins = decode(mod.image, off, ip)
            seen[ip] = ins
            if ins.kind in (K_RET, K_JMPIND, K_JMPFAR, K_HLT, K_INVALID):
                break
            if ins.kind == K_JMP:
                ip = ins.target & 0xFFFF
                continue
            if ins.kind == K_JCC:
                todo.append(ins.target & 0xFFFF)
            ip = ins.next_ip
    return seen


def callee_pops(mod, seg, target):
    """The bytes a near callee's RET n removes: 0 for plain RETs, n when every RET it reaches is RET n,
    None when they disagree. guest_call traps the return at the caller's stack level, so a RET 8
    callee called as a plain call is never trapped and the original quietly finishes the routine."""
    sizes = {int.from_bytes(i.raw[1:3], "little") if i.raw[0] == 0xC2 else 0
             for i in walk(mod, seg, target).values() if i.kind == K_RET and i.raw[0] in (0xC2, 0xC3)}
    return (sizes.pop() if sizes else 0) if len(sizes) <= 1 else None


def dossier(mod, seg, entry, insns):
    """Observed static walk, not source ownership or a parity verdict. Keep private."""
    covered, calls, exits, frontier = set(), [], [], set()
    rb = mod.reloc_bytes()
    for ip, ins in sorted(insns.items()):
        off = mod.off(seg, ip)
        covered.update(range(off, off + ins.length))
        if ins.kind in (K_CALL, K_CALLFAR, K_CALLIND):
            call = {"ip": ip, "kind": ins.kind,
                    "relocated_operand": any(off + j in rb for j in range(ins.length))}
            if ins.kind == K_CALL:
                call.update(segment=seg, offset=ins.target & 0xFFFF)
            elif ins.kind == K_CALLFAR:
                call.update(segment=ins.far_seg, offset=ins.far_off)
            calls.append(call)
        if ins.kind in (K_RET, K_JMPIND, K_JMPFAR, K_HLT, K_INVALID):
            exits.append({"ip": ip, "kind": ins.kind})
            continue
        successors = [ins.target & 0xFFFF] if ins.kind == K_JMP else [ins.next_ip]
        if ins.kind == K_JCC:
            successors.append(ins.target & 0xFFFF)
        frontier.update(dst for dst in successors if dst not in insns)
    if entry not in insns:
        frontier.add(entry)
    ranges = []
    for off in sorted(covered):
        if ranges and off == ranges[-1]["end_exclusive"]:
            ranges[-1]["end_exclusive"] += 1
        else:
            ranges.append({"start": off, "end_exclusive": off + 1})
    sources = ("tools/matched_draft.py", "recompiler/modules.py", "recompiler/x86dec.py",
               "tools/unpack.py", "recompiler/emit.py")
    root = os.path.dirname(HERE)
    return {
        "schema": 1, "module": mod.name, "segment": seg, "offset": entry,
        "image_offset": mod.off(seg, entry), "image_origin": mod.origin,
        "file_fnv1a64": "%016x" % mod.file_hash,
        "image_sha256": hashlib.sha256(mod.image).hexdigest(), "image_length": len(mod.image),
        "disassembly_provider": "capstone " + capstone.__version__ if _CS else "unavailable",
        "tool_sha256": {p: hashlib.sha256(Path(root, p).read_bytes()).hexdigest()
                        for p in sources},
        "instruction_count": len(insns), "observed_bytes": len(covered),
        "image_ranges": ranges, "calls": calls, "exits": exits,
        "unwalked_successors": sorted(frontier),
        "instructions": [{"ip": ip, "image_offset": mod.off(seg, ip), "bytes": ins.raw.hex(),
                          "kind": ins.kind, "text": text(ins)} for ip, ins in sorted(insns.items())],
        "limitations": ["Static walk only; calls are not followed and indirect targets are unresolved.",
                        "Ranges describe observed bytes, not exclusive ownership or complete discovery.",
                        "Far operands can be relocated or patched at runtime; listed targets are encoded values.",
                        "Caller inventory, runtime reachability and parity are unverified."],
    }


def draft(mod, seg, entry, name, insns=None):
    if insns is None:
        insns = walk(mod, seg, entry)
    rb = mod.reloc_bytes()
    inside = insns.__contains__
    out = ["/* DRAFT of %s %04X:%04X by tools/matched_draft.py: the original's instructions, %d of them." % (
               mod.name, seg, entry, len(insns)),
           " * Derived from the executable - rewrite it into named, explained code before it goes into",
           " * src/matched/matched.c; never commit the draft itself. */",
           "static int %s(machine_t *m)" % name, "{", "    cpu_t *c = &m->cpu;",
           "    int started_ = 0;           /* before the first instruction runs, a stop is a decline */",
           "#define CHECK(ip_) do { if (!room(c, 1)) { c->ip = (ip_); return started_; } "
           "c->op_ip = (ip_); started_ = 1; } while (0)",
           "#define IC(op_) (c->icount++)",
           "#define EXIT(ip_) do { c->ip = (uint16_t)(ip_); return 1; } while (0)",
           "#define INTERP(ip_) do { c->ip = (uint16_t)(ip_); return 1; } while (0)",
           "#define CODE8(o_) mem_read8(c, phys(c->seg[S_CS], (uint16_t)(o_)))",
           "#define CODE16(o_) seg_read16(c, c->seg[S_CS], (uint16_t)(o_))",
           "    c->op_cs = c->seg[S_CS];",
           "    goto L_%04X;" % entry]
    ordered = sorted(insns)
    for k, ip in enumerate(ordered):
        ins = insns[ip]
        nxt = ordered[k + 1] if k + 1 < len(ordered) else None
        out.append("    /* %04X  %-14s %s */" % (ip, ins.raw.hex(), text(ins)))
        if ins.kind == K_CALL:
            pops = callee_pops(mod, seg, ins.target & 0xFFFF)
            call = ("guest_call(m, 0x%04X, 0x%04X)" % (ins.target & 0xFFFF, ins.next_ip) if not pops else
                    "guest_call_pop(m, 0x%04X, 0x%04X, %d)" % (ins.target & 0xFFFF, ins.next_ip, pops))
            lines = ["L_%04X: CHECK(0x%04X);" % (ip, ip), "if (!%s) return 1;" % call]
            if pops is None:
                lines.insert(1, "/* the callee's RETs remove different argument sizes: check this call */")
            if nxt != ins.next_ip:
                lines.append("goto L_%04X;" % ins.next_ip if inside(ins.next_ip) else "EXIT(0x%04X);" % ins.next_ip)
        elif ins.kind == K_CALLFAR:
            lines = ["L_%04X: CHECK(0x%04X);" % (ip, ip),
                     "if (!guest_call_far(m, 0x%04X, 0x%04X)) return 1;" % (ip, ins.next_ip)]
            if nxt != ins.next_ip:
                lines.append("goto L_%04X;" % ins.next_ip if inside(ins.next_ip) else "EXIT(0x%04X);" % ins.next_ip)
        else:
            off = mod.off(seg, ip)
            live = frozenset(j for j in range(ins.length) if off + j in rb)
            lines = emit(ins, Ctx(inside, live))
            if ins.kind not in (K_RET, K_JMP, K_JMPIND, K_JMPFAR, K_HLT, K_INVALID) and nxt != ins.next_ip:
                lines.append("goto L_%04X;" % ins.next_ip if inside(ins.next_ip) else "EXIT(0x%04X);" % ins.next_ip)
        # Handwritten matched routines use instruction clocks; the runtime
        # bypasses them under the 386 timing profile. The shared emitter's
        # TIMING vocabulary belongs to translated regions, not this context.
        out += ["    " + l for l in lines if not l.lstrip().startswith("TIMING(")]
    out += ["#undef CHECK", "#undef IC", "#undef EXIT", "#undef INTERP", "#undef CODE8", "#undef CODE16", "}"]
    # Keep only the labels something jumps to.
    used = set(re.findall(r"goto (L_[0-9A-F]{4});", "\n".join(out)))
    out = [re.sub(r"^(\s*)(L_[0-9A-F]{4}): ", lambda mt: mt[0] if mt[2] in used else mt[1], line) for line in out]
    return "\n".join(out) + "\n", len(insns)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--data", required=True)
    ap.add_argument("--module", required=True)
    ap.add_argument("--ip", required=True, type=lambda v: int(v, 0))
    ap.add_argument("--seg", default=0, type=lambda v: int(v, 0))
    ap.add_argument("--name")
    ap.add_argument("--out", default=r"D:\f117-gate\drafts")
    ap.add_argument("--dossier", action="store_true", help="also write a private static evidence JSON")
    a = ap.parse_args()
    if not 0 <= a.seg <= 0xFFFF or not 0 <= a.ip <= 0xFFFF:
        ap.error("segment and IP must be 16-bit values")
    if a.name and not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", a.name):
        ap.error("name must be a C identifier")
    root = os.path.normcase(os.path.realpath(os.path.dirname(HERE)))
    out = os.path.realpath(a.out)
    try:
        in_repo = os.path.commonpath([root, os.path.normcase(out)]) == root
    except ValueError:  # Different Windows volumes cannot contain each other.
        in_repo = False
    if in_repo:
        ap.error("drafts are derived from the executable: write them outside the repository")
    mods = {m.name.upper(): m for m in load_all(a.data)}
    mod = mods[a.module.upper()]
    name = a.name or "%s_%04X" % (mod.name.split(".")[0].lower(), a.ip)
    insns = walk(mod, a.seg, a.ip)
    src, n = draft(mod, a.seg, a.ip, name, insns)
    os.makedirs(out, exist_ok=True)
    path = os.path.join(out, name + ".c")
    open(path, "w", newline="\n").write(src)
    if a.dossier:
        with open(os.path.join(out, name + ".json"), "w", encoding="utf-8", newline="\n") as f:
            json.dump(dossier(mod, a.seg, a.ip, insns), f, indent=2)
            f.write("\n")
    print("%s: %d instructions -> %s" % (name, n, path))


if __name__ == "__main__":
    main()
