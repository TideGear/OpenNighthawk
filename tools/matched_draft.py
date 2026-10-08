#!/usr/bin/env python3
"""matched_draft.py - a starting point for a Phase 2 matched routine: the original's instructions as C.

    py tools/matched_draft.py --data GOG_DIR --module VGAME.EXE --ip 0xC6B3 [--seg 0] [--name vgame_dist]
                              [--out DIR]

Walks the routine from its entry (fall-through, jumps and conditional jumps; calls continue after the
call; RET, indirect jumps and far jumps end a path) and writes one function in the shape
src/matched/matched.c expects: each instruction as the translator emits it (recompiler/emit.py, the
interpreter's own semantics), a decline before each instruction when the run loop must look at events
(the entry declines, so the original runs; later ones stop with IP there, so the original finishes),
the clock per instruction, and near and far calls through guest_call. It is equal to the original by
construction and passes tests/func_lockstep.c as written, so it can then be rewritten into named,
explained code with the lockstep green at every step.

THE DRAFT IS DERIVED FROM THE ORIGINAL EXECUTABLE, like the translation: it is written to --out
(default D:/f117-gate/drafts), never into the repository. Only the rewritten routine - named, its
logic explained, not a transcript of instructions - goes into src/matched/matched.c.
"""
import argparse
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(HERE), "recompiler"))
from emit import Ctx, emit                                     # noqa: E402
from modules import load_all                                    # noqa: E402
from x86dec import (decode, K_CALL, K_CALLFAR, K_HLT, K_INVALID, K_JCC, K_JMP, K_JMPFAR,  # noqa: E402
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


def draft(mod, seg, entry, name):
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
           "#define IC() (c->icount++)",
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
            lines = ["L_%04X: CHECK(0x%04X);" % (ip, ip),
                     "if (!guest_call(m, 0x%04X, 0x%04X)) return 1;" % (ins.target & 0xFFFF, ins.next_ip)]
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
        out += ["    " + l for l in lines]
    out += ["#undef CHECK", "#undef IC", "#undef EXIT", "#undef INTERP", "#undef CODE8", "#undef CODE16", "}"]
    return "\n".join(out) + "\n", len(insns)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--data", required=True)
    ap.add_argument("--module", required=True)
    ap.add_argument("--ip", required=True, type=lambda v: int(v, 0))
    ap.add_argument("--seg", default=0, type=lambda v: int(v, 0))
    ap.add_argument("--name")
    ap.add_argument("--out", default=r"D:\f117-gate\drafts")
    a = ap.parse_args()
    mods = {m.name.upper(): m for m in load_all(a.data)}
    mod = mods[a.module.upper()]
    name = a.name or "%s_%04X" % (mod.name.split(".")[0].lower(), a.ip)
    src, n = draft(mod, a.seg, a.ip, name)
    root = os.path.normcase(os.path.abspath(os.path.dirname(HERE)))
    out = os.path.abspath(a.out)
    if os.path.normcase(out).startswith(root):
        sys.exit("drafts are derived from the executable: write them outside the repository")
    os.makedirs(out, exist_ok=True)
    path = os.path.join(out, name + ".c")
    open(path, "w", newline="\n").write(src)
    print("%s: %d instructions -> %s" % (name, n, path))


if __name__ == "__main__":
    main()
