"""discover.py - finding the code in a module and cutting it into regions.

A region is the closure of one seed under near control flow (fall-through,
jumps, conditional branches, switch tables), never crossing a call: a call's
target seeds a region of its own, and its return address is reached by the
fall-through. Every instruction belongs to exactly one region - the first to
reach it - and flow from a region into another region's instruction leaves
through the dispatcher. That makes the translation correct whatever the
seeds were: a region that starts mid-function, or one seeded by a guess that
decodes data, still translates each instruction faithfully, because an
instruction's translation depends only on its bytes and its code segment.
What the seeds decide is only how much runs translated rather than
interpreted.

Seeds, strongest first:
  1. the entry points the file declares (program entry, overlay tables);
  2. call targets and far branch targets found while walking;
  3. coverage: instructions the interpreter executed in recorded runs;
  4. heuristics: far pointers in relocated data, and Microsoft C function
     prologues inside the code segments.
"""
from __future__ import annotations

import struct
from collections import deque
from dataclasses import dataclass, field

from x86dec import (DecodeError, K_CALL, K_CALLFAR, K_JMPFAR, K_JMPIND, S_CS,
                    decode, successors)


@dataclass
class Region:
    seg: int
    seed_ip: int
    insns: dict = field(default_factory=dict)      # ip -> Insn
    live: dict = field(default_factory=dict)       # ip -> set of operand byte offsets read at run time


class Discovery:
    def __init__(self, mod, log=print):
        self.m = mod
        self.log = log
        self.owner = {}            # (seg, ip) -> Region
        self.regions = []
        self.reloc_bytes = mod.reloc_bytes()
        self.code_segs = set(s for s, _ in mod.entries)
        self.pending = deque()     # (seg, ip, why)
        self.seen_seed = set()
        self.bad = set()           # (seg, ip) that did not decode
        self.stats = {"tables": 0, "table_targets": 0}

    # ---- helpers -----------------------------------------------------------
    def in_image(self, seg, ip, n=1):
        off = self.m.off(seg, ip)
        return 0 <= off and off + n <= len(self.m.image)

    def decode_at(self, seg, ip):
        off = self.m.off(seg, ip)
        if off < 0 or off >= len(self.m.image):
            return None
        # An instruction cannot wrap past the end of its code segment.
        limit = min(len(self.m.image), off + (0x10000 - ip))
        try:
            ins = decode(self.m.image, off, ip, limit)
        except DecodeError:
            return None
        return ins

    def live_bytes(self, seg, ins):
        """Operand bytes of `ins` that the loader relocates: the translation
        reads them from memory as the CPU would. Far branch operands are
        always read live: they are where the game patches its interrupt
        chains, and the segment is relocated anyway."""
        off = self.m.off(seg, ins.ip)
        live = set()
        for k in range(ins.length):
            if off + k in self.reloc_bytes:
                live.add(k)
        if ins.op in (0x9A, 0xEA):
            live.update(range(ins.imm_off, ins.imm_off + 4))
        return live

    def add_seed(self, seg, ip, why):
        key = (seg, ip)
        if key in self.seen_seed:
            return
        self.seen_seed.add(key)
        self.pending.append((seg, ip, why))

    def far_target(self, ins, seg):
        """The module-relative target of a far call or jump, when its segment
        word is a relocation site (so it names a segment of this module)."""
        off = self.m.off(seg, ins.ip)
        seg_word_off = off + ins.imm2_off
        if seg_word_off in self.m.relocs:
            return ins.far_seg, ins.far_off
        return None

    def switch_table(self, region, seg, ins):
        """Targets of `jmp word ptr cs:[reg+table]`: Microsoft C's switch.
        The bound comes from the `cmp reg, N` that guards it when one is
        found just before; otherwise entries are taken while they point
        inside the module's code and not into the table itself."""
        m = ins.modrm
        if ins.seg_ovr != S_CS or m is None or m.is_reg or m.mod == 0 or m.rm not in (4, 5, 7):
            return []
        table = m.disp
        count = None
        # Look back a few instructions for the bound.
        prev = sorted((ip for ip in region.insns if ip < ins.ip), reverse=True)[:8]
        for pip in prev:
            p = region.insns[pip]
            if p.op == 0x3D or (p.op in (0x81, 0x83) and p.modrm and p.modrm.reg == 7 and p.modrm.is_reg):
                count = (p.imm if p.op != 0x83 else (p.imm - 256 if p.imm >= 0x80 else p.imm)) + 1
                break
        if count is None or not (0 < count <= 512):
            count = 256
            guessed = True
        else:
            guessed = False
        targets = []
        for k in range(count):
            a = table + 2 * k
            if not self.in_image(seg, a, 2):
                break
            o = self.m.off(seg, a)
            if o in self.reloc_bytes:
                break
            t = struct.unpack("<H", self.m.image[o:o + 2])[0]
            if guessed:
                # stop at anything that cannot be a case label
                if not self.in_image(seg, t) or table <= t < table + 2 * count or t < 2:
                    break
                if self.decode_at(seg, t) is None:
                    break
            targets.append(t)
        if targets:
            self.stats["tables"] += 1
            self.stats["table_targets"] += len(targets)
        return targets

    def terminates(self, r, seg, ins):
        """A DOS terminate does not return: INT 20h, or INT 21h straight
        after `mov ah, 4Ch` / `mov ax, 4Cxxh` in the same walk. Whatever
        follows is data (F117.COM keeps its variables right after its exit
        call), and decoding it as code would make every write to those
        variables invalidate the region."""
        if ins.op != 0xCD:
            return False
        if ins.imm == 0x20:
            return True
        if ins.imm != 0x21:
            return False
        p2 = r.insns.get((ins.ip - 2) & 0xFFFF)
        if p2 is not None and p2.raw == b"\xB4\x4C":
            return True
        p3 = r.insns.get((ins.ip - 3) & 0xFFFF)
        return p3 is not None and p3.raw[:1] == b"\xB8" and p3.raw[2:3] == b"\x4C"

    # ---- the walk -------------------------------------------------------------
    def grow(self, seg, ip, why):
        if (seg, ip) in self.owner or (seg, ip) in self.bad:
            return
        first = self.decode_at(seg, ip)
        if first is None:
            self.bad.add((seg, ip))
            return
        r = Region(seg=seg, seed_ip=ip)
        self.regions.append(r)
        stack = [ip]
        while stack:
            x = stack.pop()
            key = (seg, x)
            if key in self.owner or key in self.bad:
                continue
            ins = self.decode_at(seg, x)
            if ins is None:
                self.bad.add(key)
                continue
            self.owner[key] = r
            r.insns[x] = ins
            r.live[x] = self.live_bytes(seg, ins)
            if not self.terminates(r, seg, ins):
                for s in successors(ins):
                    stack.append(s)
            if ins.kind == K_CALL:
                self.add_seed(seg, ins.target, "call")
            elif ins.kind in (K_CALLFAR, K_JMPFAR):
                t = self.far_target(ins, seg)
                if t:
                    self.code_segs.add(t[0])
                    self.add_seed(t[0], t[1], "far")
            elif ins.kind == K_JMPIND:
                for t in self.switch_table(r, seg, ins):
                    stack.append(t)

    def drain(self):
        while self.pending:
            seg, ip, why = self.pending.popleft()
            self.grow(seg, ip, why)

    def run(self, coverage=(), heuristics=True):
        for seg, ip in self.m.entries:
            self.add_seed(seg, ip, "entry")
        self.drain()
        n_static = sum(len(r.insns) for r in self.regions)
        for seg, ip in coverage:
            self.add_seed(seg, ip, "coverage")
        self.drain()
        n_cov = sum(len(r.insns) for r in self.regions) - n_static
        n_heur = 0
        if heuristics:
            before = sum(len(r.insns) for r in self.regions)
            self.far_pointer_seeds()
            self.drain()
            self.prologue_seeds()
            self.drain()
            n_heur = sum(len(r.insns) for r in self.regions) - before
        self.log("  %-12s %6d bytes  %4d regions  %6d instructions (%d static, %d coverage, %d heuristic)"
                 "  %d switch tables" % (
                     self.m.name, len(self.m.image), len(self.regions),
                     sum(len(r.insns) for r in self.regions), n_static, n_cov, n_heur,
                     self.stats["tables"]))
        return self.regions

    # ---- heuristics -------------------------------------------------------------
    @staticmethod
    def looks_like_prologue(b):
        return b[:3] == b"\x55\x8B\xEC" or (b[:1] == b"\xC8" and len(b) >= 4 and b[3] == 0)

    def far_pointer_seeds(self):
        """Far code pointers in data carry a relocation on their segment
        word: a segment of this module, with the offset just before it."""
        img = self.m.image
        for r in sorted(self.m.relocs):
            if r < 2 or r + 2 > len(img):
                continue
            seg = img[r] | (img[r + 1] << 8)
            if seg not in self.code_segs:
                continue
            ip = img[r - 2] | (img[r - 1] << 8)
            o = self.m.off(seg, ip)
            if 0 <= o < len(img) and self.looks_like_prologue(img[o:o + 4]):
                self.add_seed(seg, ip, "farptr")

    def prologue_seeds(self):
        """`push bp; mov bp, sp` or `enter n, 0` in the code segments that the
        walk did not reach: functions reached only through near pointers."""
        if self.m.kind != "exe" or self.m.stack_seg <= 0:
            return
        img = self.m.image
        end = min(len(img), self.m.stack_seg * 16)
        segs = sorted(self.code_segs)
        covered = set()
        for (s, ip), r in self.owner.items():
            o = self.m.off(s, ip)
            ins = r.insns[ip]
            covered.update(range(o, o + ins.length))
        for o in range(0, end - 3):
            if o in covered or not self.looks_like_prologue(img[o:o + 4]):
                continue
            seg = max((s for s in segs if s * 16 <= o), default=None)
            if seg is None or o - seg * 16 > 0xFFFF:
                continue
            self.add_seed(seg, o - seg * 16 + self.m.origin, "prologue")
