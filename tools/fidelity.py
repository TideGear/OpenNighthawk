#!/usr/bin/env python3
"""fidelity.py - the emulated PC held to GOG DOSBox, the reference machine.

    py tools/fidelity.py --data INSTALL_DIR [--dosbox DOSBox.exe] [--keep]

The interpreter and the recompiled code run on the same model of a PC, so
a difference between that model and the machine the game shipped on (GOG's
DOSBox 0.74-2 with dosboxF117A.conf) cannot show up as a disagreement
between them. This measures the model against the reference directly.

A probe program, assembled here, asks the machine what the game asks it
(the services and ports in the inventory, F117R_INVENTORY): the DOS version,
the PSP, the environment, free memory, device information, the BIOS data
area, the video mode, the mouse driver, the keyboard, the interrupt mask,
the AdLib timer detection, the MPU-401 reset, the joystick port, the VGA
registers, and the clocks - the PIT, the VGA retrace period in PIT counts,
how many instructions run per PIT count, what a port access costs. It runs
as F117.COM from the same autoexec GOG uses (keyb us, cls, then the
program), under GOG's DOSBox headless and under f117run, writes its answers
to OUT.BIN, and the two answer sheets are compared field by field.

Nothing from the game is copied into the probe; it only needs the
install's DOSBox and configuration.
"""
from __future__ import annotations

import argparse
import os
import shutil
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

# ---------------------------------------------------------------------------
# A tiny 16-bit assembler: only the forms the probe uses, encoded by hand.
# ---------------------------------------------------------------------------
R16 = {"ax": 0, "cx": 1, "dx": 2, "bx": 3, "sp": 4, "bp": 5, "si": 6, "di": 7}
R8 = {"al": 0, "cl": 1, "dl": 2, "bl": 3, "ah": 4, "ch": 5, "dh": 6, "bh": 7}
SREG = {"es": 0, "cs": 1, "ss": 2, "ds": 3}
JCC = {"jo": 0x70, "jno": 0x71, "jb": 0x72, "jc": 0x72, "jae": 0x73, "jnc": 0x73, "jz": 0x74, "je": 0x74,
       "jnz": 0x75, "jne": 0x75, "jbe": 0x76, "ja": 0x77, "js": 0x78, "jns": 0x79}


class Asm:
    def __init__(self, origin=0x100):
        self.origin = origin
        self.b = bytearray()
        self.labels = {}
        self.fix = []            # (pos, label, kind)

    @property
    def here(self):
        return self.origin + len(self.b)

    def label(self, name):
        assert name not in self.labels, name
        self.labels[name] = self.here

    def db(self, *bs):
        for x in bs:
            if isinstance(x, (bytes, bytearray)):
                self.b += x
            else:
                self.b.append(x & 0xFF)

    def dw(self, v):
        self.b += struct.pack("<H", v & 0xFFFF)

    def ref16(self, label):
        """A 16-bit absolute address of a label, fixed up at the end."""
        self.fix.append((len(self.b), label, "abs"))
        self.dw(0)

    # mov
    def mov_r16_imm(self, r, v):
        self.db(0xB8 + R16[r]); self.dw(v)

    def mov_r16_label(self, r, label):
        self.db(0xB8 + R16[r]); self.ref16(label)

    def mov_r8_imm(self, r, v):
        self.db(0xB0 + R8[r], v)

    def mov_rr16(self, dst, src):
        self.db(0x89, 0xC0 | (R16[src] << 3) | R16[dst])

    def mov_rr8(self, dst, src):
        self.db(0x88, 0xC0 | (R8[src] << 3) | R8[dst])

    def mov_r16_sreg(self, r, s):
        self.db(0x8C, 0xC0 | (SREG[s] << 3) | R16[r])

    def mov_sreg_r16(self, s, r):
        self.db(0x8E, 0xC0 | (SREG[s] << 3) | R16[r])

    def store(self, label, r):
        """mov [label], r16"""
        self.db(0x2E, 0x89, (R16[r] << 3) | 6); self.ref16(label)

    def load(self, r, label):
        """mov r16, [label]"""
        self.db(0x2E, 0x8B, (R16[r] << 3) | 6); self.ref16(label)

    def load_es_abs(self, r, addr):
        """mov r16, es:[addr]"""
        self.db(0x26, 0x8B, (R16[r] << 3) | 6); self.dw(addr)

    def load_es_abs8(self, r8, addr):
        self.db(0x26, 0x8A, (R8[r8] << 3) | 6); self.dw(addr)

    # arithmetic
    def xor_rr16(self, a, b):
        self.db(0x31, 0xC0 | (R16[b] << 3) | R16[a])

    def sub_rr16(self, a, b):
        self.db(0x29, 0xC0 | (R16[b] << 3) | R16[a])

    def add_r16_imm(self, r, v):
        self.db(0x81, 0xC0 | R16[r]); self.dw(v)

    def inc16(self, r):
        self.db(0x40 + R16[r])

    def dec16(self, r):
        self.db(0x48 + R16[r])

    def test_al(self, v):
        self.db(0xA8, v)

    def and_al(self, v):
        self.db(0x24, v)

    def cmp_r16_imm(self, r, v):
        self.db(0x81, 0xF8 | R16[r]); self.dw(v)

    # i/o
    def out_imm(self, port, v):
        """mov al, v ; out port, al (port < 256) or via DX"""
        self.mov_r8_imm("al", v)
        if port < 0x100:
            self.db(0xE6, port)
        else:
            self.mov_r16_imm("dx", port); self.db(0xEE)

    def in_al(self, port):
        if port < 0x100:
            self.db(0xE4, port)
        else:
            self.mov_r16_imm("dx", port); self.db(0xEC)

    def in_al_dx(self):
        self.db(0xEC)

    def out_dx_al(self):
        self.db(0xEE)

    # flow
    def int_(self, n):
        self.db(0xCD, n)

    def jmp(self, label):
        self.db(0xE9); self.fix.append((len(self.b), label, "rel16")); self.dw(0)

    def call(self, label):
        self.db(0xE8); self.fix.append((len(self.b), label, "rel16")); self.dw(0)

    def ret(self):
        self.db(0xC3)

    def jcc(self, cc, label):
        self.db(JCC[cc]); self.fix.append((len(self.b), label, "rel8")); self.db(0)

    def loop(self, label):
        self.db(0xE2); self.fix.append((len(self.b), label, "rel8")); self.db(0)

    def pushf_pop(self, r):
        self.db(0x9C, 0x58 + R16[r])

    def push(self, r):
        self.db(0x50 + R16[r])

    def pop(self, r):
        self.db(0x58 + R16[r])

    def cli(self):
        self.db(0xFA)

    def sti(self):
        self.db(0xFB)

    def cld(self):
        self.db(0xFC)

    def link(self):
        for pos, label, kind in self.fix:
            t = self.labels[label]
            if kind == "abs":
                struct.pack_into("<H", self.b, pos, t)
            elif kind == "rel16":
                struct.pack_into("<H", self.b, pos, (t - (self.origin + pos + 2)) & 0xFFFF)
            else:
                d = t - (self.origin + pos + 1)
                assert -128 <= d < 128, (label, d)
                self.b[pos] = d & 0xFF
        return bytes(self.b)


# ---------------------------------------------------------------------------
# The probe.
# ---------------------------------------------------------------------------
FIELDS = []          # (name, kind) in result order; kind: "exact" or "rate"
MCB_BYTES = 24 * 18  # up to 24 memory control blocks: segment + 16 header bytes
LOW_BYTES = 0x1910   # linear 0000:0000 up to the program's PSP in DOSBox


def build_probe():
    a = Asm()
    a.jmp("start")
    a.label("start")
    slot = [0]

    def rec(name, r="ax", kind="exact"):
        lab = "res_%d" % slot[0]
        slot[0] += 1
        FIELDS.append((name, kind))
        a.store(lab, r)

    def flags_cf(name):
        a.pushf_pop("ax"); a.db(0x25); a.dw(0x0001)        # and ax, 1
        rec(name)

    def snapshot(name, skip=()):
        """Every register and the flags, as they are now. Fields named in
        skip hold values that legitimately differ (clock readings,
        addresses of the machines' own code)."""
        for r in ("ax", "bx", "cx", "dx", "si", "di", "bp", "sp"):
            rec("%s %s" % (name, r.upper()), r, "ignore" if r.upper() in skip else "exact")
        a.pushf_pop("ax"); a.db(0x25); a.dw(0x0FD5 | 0x0200 | 0x0400)   # arithmetic, IF, DF
        rec("%s FLAGS" % name, "ax", "ignore" if "FLAGS" in skip else "exact")
        for s in ("ds", "es"):
            a.mov_r16_sreg("ax", s)
            rec("%s %s" % (name, s.upper()), "ax", "ignore" if s.upper() in skip else "exact")
        a.mov_r16_sreg("ax", "cs"); a.mov_sreg_r16("ds", "ax"); a.mov_sreg_r16("es", "ax")

    def call(name, vec, regs, skip=(), es=None, keep=None):
        """Set registers (others to distinct sentinels), raise the interrupt,
        record everything."""
        init = {"ax": 0xA1A1, "bx": 0xB2B2, "cx": 0xC3C3, "dx": 0xD4D4, "si": 0x5E5E, "di": 0xD1D1, "bp": 0xB9B9}
        init.update(regs)
        a.mov_r16_imm("ax", 0x0202); a.push("ax"); a.db(0x9D)     # known flags going in: IF only
        for r in ("ax", "bx", "cx", "dx", "si", "di", "bp"):
            v = init[r]
            if isinstance(v, str):
                a.mov_r16_label(r, v)
            else:
                a.mov_r16_imm(r, v)
        if es is not None:
            a.push("ax"); a.mov_r16_imm("ax", es); a.mov_sreg_r16("es", "ax"); a.pop("ax")
        a.int_(vec)
        if keep:
            a.store(keep, "ax")          # the result AX, before the snapshot uses AX
        snapshot(name, skip)

    # --- how the program is started: registers, flags, stack ----------------
    snapshot("entry")
    a.cld()
    # the low memory as found: copied before any service is called
    a.push("ds"); a.xor_rr16("si", "si"); a.mov_sreg_r16("ds", "si")
    a.mov_r16_label("di", "lowcopy"); a.mov_r16_imm("cx", LOW_BYTES)
    a.db(0xF3, 0xA4)                                  # rep movsb (ES = CS)
    a.pop("ds")

    # --- the memory control block chain, before anything is changed --------
    # INT 21h/52h: the first MCB's segment is the word before ES:BX. Each of
    # up to 24 blocks is recorded as its segment and its 16 header bytes.
    a.mov_r16_imm("ax", 0x5200); a.int_(0x21)
    a.db(0x26, 0x8B, 0x47, 0xFE)                     # mov ax, es:[bx-2]
    a.mov_r16_label("di", "mcbbuf")
    a.mov_r16_imm("bp", 24)
    a.label("mcb_l")
    a.mov_sreg_r16("es", "ax")
    a.db(0x89, 0x05)                                 # mov [di], ax
    for k in range(8):
        a.load_es_abs("dx", 2 * k)
        a.db(0x89, 0x55, 2 + 2 * k)                  # mov [di+2+2k], dx
    a.add_r16_imm("di", 18)
    a.load_es_abs("dx", 3); a.add_r16_imm("ax", 1); a.db(0x01, 0xD0)   # ax += 1 + size
    a.load_es_abs8("dl", 0); a.db(0x80, 0xFA, ord("Z")); a.jcc("je", "mcb_done")
    a.dec16("bp"); a.jcc("jnz", "mcb_l")
    a.label("mcb_done")
    a.mov_r16_sreg("ax", "cs"); a.mov_sreg_r16("es", "ax")

    def vga_registers(tag):
        """Every sequencer, CRTC, graphics and attribute register, and the
        miscellaneous output, as the mode set left them."""
        for idx in range(5):
            a.out_imm(0x3C4, idx); a.in_al(0x3C5); a.mov_r8_imm("ah", 0); rec("seq %d (%s)" % (idx, tag))
        for idx in range(0x19):
            a.out_imm(0x3D4, idx); a.in_al(0x3D5); a.mov_r8_imm("ah", 0); rec("crtc %02X (%s)" % (idx, tag))
        for idx in range(9):
            a.out_imm(0x3CE, idx); a.in_al(0x3CF); a.mov_r8_imm("ah", 0); rec("gc %d (%s)" % (idx, tag))
        for idx in range(0x15):
            a.in_al(0x3DA)                                     # attribute flip-flop to index
            a.out_imm(0x3C0, idx | 0x20); a.in_al(0x3C1); a.mov_r8_imm("ah", 0); rec("attr %02X (%s)" % (idx, tag))
        a.in_al(0x3DA)
        a.in_al(0x3CC); a.mov_r8_imm("ah", 0); rec("misc output (%s)" % tag)

    # --- the program's own view of DOS ------------------------------------
    a.mov_r16_sreg("ax", "cs"); rec("psp segment")
    a.load("ax", "psp_env"); rec("PSP:2C environment segment")
    a.mov_r16_imm("ax", 0x3000); a.int_(0x21); rec("dos version AX"); rec("dos version BX", "bx"); rec("dos version CX", "cx")
    a.mov_r16_imm("ax", 0x3306); a.xor_rr16("bx", "bx"); a.int_(0x21); rec("true version BX (3306)", "bx")
    for h in range(5):
        a.mov_r16_imm("ax", 0x4400); a.mov_r16_imm("bx", h); a.xor_rr16("dx", "dx"); a.int_(0x21)
        rec("ioctl 4400 handle %d DX" % h, "dx")
    # free memory: the largest block before and after shrinking to 64 KB
    a.mov_r16_imm("ax", 0x4800); a.mov_r16_imm("bx", 0xFFFF); a.int_(0x21); rec("largest free block (paragraphs), unshrunk", "bx")
    a.mov_r16_sreg("ax", "cs"); a.mov_sreg_r16("es", "ax")
    a.mov_r16_imm("ax", 0x4A00); a.mov_r16_imm("bx", 0x1000); a.int_(0x21); flags_cf("shrink to 64K CF")
    a.mov_r16_imm("ax", 0x4800); a.mov_r16_imm("bx", 0xFFFF); a.int_(0x21); rec("largest free block (paragraphs), after shrink", "bx")
    a.int_(0x11); rec("int 11 equipment")
    a.int_(0x12); rec("int 12 memory KB")
    a.mov_r16_imm("ax", 0x3508); a.int_(0x21); a.mov_r16_sreg("ax", "es"); rec("int 08 vector segment nonzero", kind="nonzero")
    # BIOS data area
    a.xor_rr16("ax", "ax"); a.mov_sreg_r16("es", "ax")
    for off, nm in ((0x410, "BDA equipment"), (0x413, "BDA memory KB"), (0x449, "BDA video mode (byte) + columns lo"),
                    (0x44A, "BDA columns"), (0x44C, "BDA page size"), (0x463, "BDA CRTC port"),
                    (0x465, "BDA mode select + palette"), (0x484, "BDA rows-1 + char height lo"),
                    (0x487, "BDA EGA misc"), (0x417, "BDA keyboard flags"), (0x41A, "BDA key head"),
                    (0x41C, "BDA key tail"), (0x480, "BDA key buffer start"), (0x482, "BDA key buffer end"),
                    (0x496, "BDA keyboard mode/type")):
        a.load_es_abs("ax", off); rec(nm)
    # --- BIOS video ---------------------------------------------------------
    a.mov_r16_imm("ax", 0x0F00); a.mov_r16_imm("bx", 0x1234); a.int_(0x10); rec("int 10/0F AX (text)"); rec("int 10/0F BX (text)", "bx")
    a.mov_r16_imm("ax", 0x0300); a.xor_rr16("bx", "bx"); a.int_(0x10); rec("cursor position DX", "dx"); rec("cursor shape CX", "cx")
    a.mov_r16_imm("ax", 0x1A00); a.int_(0x10); rec("int 10/1A AX"); rec("int 10/1A BX", "bx")
    a.mov_r16_imm("ax", 0x0003); a.int_(0x10)
    vga_registers("mode 3")
    a.mov_r16_imm("ax", 0x0013); a.int_(0x10)
    a.mov_r16_imm("ax", 0x0F00); a.int_(0x10); rec("int 10/0F AX (13h)"); rec("int 10/0F BX (13h)", "bx")
    vga_registers("13h")
    a.in_al(0x3C6); a.mov_r8_imm("ah", 0); rec("pel mask")
    # DAC: palette entry 7 after mode set, then a write and read back
    a.out_imm(0x3C7, 7); a.mov_r16_imm("dx", 0x3C9)
    for k in range(3):
        a.in_al_dx(); a.mov_r8_imm("ah", 0); rec("DAC 7 component %d after mode set" % k)
    a.out_imm(0x3C8, 1); a.mov_r16_imm("dx", 0x3C9)
    for v in (0x3F, 0x7F, 0xC1):
        a.mov_r8_imm("al", v); a.out_dx_al()
    a.out_imm(0x3C7, 1); a.mov_r16_imm("dx", 0x3C9)
    for k in range(3):
        a.in_al_dx(); a.mov_r8_imm("ah", 0); rec("DAC write 3F/7F/C1 read back %d" % k)
    a.in_al(0x3C7); a.mov_r8_imm("ah", 0); rec("DAC state 3C7")
    # palette block read (int 10/1017) of entry 1
    a.mov_r16_imm("ax", 0x1015); a.mov_r16_imm("bx", 1); a.int_(0x10); rec("int 10/1015 DH", "dx"); rec("int 10/1015 CX", "cx")
    a.mov_r16_imm("ax", 0x0003); a.int_(0x10)
    # --- mouse --------------------------------------------------------------
    a.xor_rr16("ax", "ax"); a.int_(0x33); rec("int 33/00 AX"); rec("int 33/00 BX", "bx")
    a.mov_r16_imm("ax", 0x0003); a.int_(0x33); rec("int 33/03 buttons", "bx"); rec("int 33/03 X", "cx"); rec("int 33/03 Y", "dx")
    a.mov_r16_imm("ax", 0x0013); a.int_(0x10)
    a.xor_rr16("ax", "ax"); a.int_(0x33)
    a.mov_r16_imm("ax", 0x0003); a.int_(0x33); rec("int 33/03 X in 13h", "cx"); rec("int 33/03 Y in 13h", "dx")
    a.mov_r16_imm("ax", 0x0007); a.mov_r16_imm("cx", 0); a.mov_r16_imm("dx", 639); a.int_(0x33)
    a.mov_r16_imm("ax", 0x0008); a.mov_r16_imm("cx", 0); a.mov_r16_imm("dx", 199); a.int_(0x33)
    a.mov_r16_imm("ax", 0x0004); a.mov_r16_imm("cx", 700); a.mov_r16_imm("dx", 300); a.int_(0x33)
    a.mov_r16_imm("ax", 0x0003); a.int_(0x33); rec("int 33/03 X after set 700 (clamped)", "cx"); rec("int 33/03 Y after set 300 (clamped)", "dx")
    a.mov_r16_imm("ax", 0x000F); a.mov_r16_imm("cx", 8); a.mov_r16_imm("dx", 16); a.int_(0x33); rec("int 33/0F AX")
    a.mov_r16_imm("ax", 0x000B); a.int_(0x33); rec("int 33/0B mickeys X", "cx", "zeroish"); rec("int 33/0B mickeys Y", "dx", "zeroish")
    # The software cursor must be visible to reads from VGA memory, and
    # hiding must restore what was under it, including after guest writes.
    def mouse(fn, x=None, y=None):
        a.mov_r16_imm("ax", fn)
        if x is not None: a.mov_r16_imm("cx", x)
        if y is not None: a.mov_r16_imm("dx", y)
        a.int_(0x33)

    def pixel_read(name, x=20, y=20):
        a.db(0x06)  # push es
        a.mov_r16_imm("ax", 0xA000); a.mov_sreg_r16("es", "ax")
        a.db(0x26, 0xA0); a.dw(y * 320 + x)  # mov al, es:[pixel]
        a.db(0x07)  # pop es
        a.mov_r8_imm("ah", 0); rec(name)

    def pixel_write(colour, x=20, y=20):
        a.db(0x06)
        a.mov_r16_imm("ax", 0xA000); a.mov_sreg_r16("es", "ax")
        a.db(0x26, 0xC6, 0x06); a.dw(y * 320 + x); a.db(colour)
        a.db(0x07)

    mouse(0)
    mouse(4, 40, 20)
    pixel_write(0x7A)
    a.mov_r16_sreg("ax", "ds"); a.mov_sreg_r16("es", "ax")
    a.mov_r16_label("dx", "cursor_masks")
    a.xor_rr16("bx", "bx"); a.xor_rr16("cx", "cx")
    a.mov_r16_imm("ax", 9); a.int_(0x33)
    mouse(1); pixel_read("mouse cursor: XOR pixel visible in VRAM")
    pixel_write(0x33); pixel_read("mouse cursor: guest overwrite visible")
    mouse(2); pixel_read("mouse cursor: hide restores saved background")
    mouse(2); mouse(1); pixel_read("mouse cursor: nested hide still hidden")
    mouse(1); pixel_read("mouse cursor: balanced show draws again")
    mouse(4, 44, 20); pixel_read("mouse cursor: move restores old position")
    pixel_read("mouse cursor: move draws new position", 22, 20)
    mouse(0); pixel_read("mouse cursor: reset restores background", 22, 20)
    a.mov_r16_imm("ax", 0x0003); a.int_(0x10)
    # --- keyboard -----------------------------------------------------------
    a.mov_r16_imm("ax", 0x0100); a.int_(0x16); a.pushf_pop("ax"); a.db(0x25); a.dw(0x0040); rec("int 16/01 ZF (empty)")
    a.mov_r16_imm("ax", 0x0200); a.int_(0x16); a.mov_r8_imm("ah", 0); rec("int 16/02 shift flags")
    a.mov_r16_imm("ax", 0x0B00); a.int_(0x21); a.mov_r8_imm("ah", 0); rec("int 21/0B stdin status")
    # --- interrupt controller, keyboard controller, port 61 ----------------
    a.in_al(0x21); a.mov_r8_imm("ah", 0); rec("PIC IMR")
    a.out_imm(0x20, 0x0B); a.in_al(0x20); a.mov_r8_imm("ah", 0); rec("PIC ISR (OCW3 0B)")
    a.out_imm(0x20, 0x0A); a.in_al(0x20); a.mov_r8_imm("ah", 0); a.and_al(0xFE); rec("PIC IRR (OCW3 0A) without IRQ0")
    a.in_al(0x61); a.mov_r8_imm("ah", 0); a.and_al(0x0F); rec("port 61 low bits")
    a.in_al(0x64); a.mov_r8_imm("ah", 0); rec("port 64 status")
    # --- joystick port ---------------------------------------------------------
    a.in_al(0x201); a.mov_r8_imm("ah", 0); rec("joystick 201 idle")
    # --- other ports the programs read ------------------------------------------
    for p in (0x0B8B, 0x022A, 0x022E, 0x0389, 0x03C2):
        a.in_al(p); a.mov_r8_imm("ah", 0); rec("port %04X" % p)
    # --- MPU-401 ----------------------------------------------------------------
    a.in_al(0x331); a.mov_r8_imm("ah", 0); rec("MPU status idle")
    a.out_imm(0x331, 0xFF)
    a.mov_r16_imm("cx", 2000); a.label("mpu_w"); a.mov_r16_imm("dx", 0x331); a.in_al_dx(); a.test_al(0x80); a.jcc("jz", "mpu_r"); a.loop("mpu_w")
    a.label("mpu_r")
    a.mov_r16_imm("dx", 0x330); a.in_al_dx(); a.mov_r8_imm("ah", 0); rec("MPU reset reply")
    a.out_imm(0x331, 0x3F)
    a.mov_r16_imm("cx", 2000); a.label("mpu_w2"); a.mov_r16_imm("dx", 0x331); a.in_al_dx(); a.test_al(0x80); a.jcc("jz", "mpu_r2"); a.loop("mpu_w2")
    a.label("mpu_r2")
    a.mov_r16_imm("dx", 0x330); a.in_al_dx(); a.mov_r8_imm("ah", 0); rec("MPU UART-mode reply")
    a.out_imm(0x331, 0xFF)       # leave intelligent mode reset
    a.mov_r16_imm("cx", 2000); a.label("mpu_w3"); a.mov_r16_imm("dx", 0x331); a.in_al_dx(); a.test_al(0x80); a.jcc("jz", "mpu_r3"); a.loop("mpu_w3")
    a.label("mpu_r3"); a.mov_r16_imm("dx", 0x330); a.in_al_dx()

    # --- AdLib: the timer detection every driver uses ------------------------
    def opl(reg, val):
        a.mov_r16_imm("dx", 0x388); a.mov_r8_imm("al", reg); a.out_dx_al()
        for _ in range(6):
            a.in_al_dx()
        a.mov_r16_imm("dx", 0x389); a.mov_r8_imm("al", val); a.out_dx_al()
        a.mov_r16_imm("dx", 0x388)
        for _ in range(35):
            a.in_al_dx()
    opl(4, 0x60); opl(4, 0x80)
    a.in_al(0x388); a.mov_r8_imm("ah", 0); a.and_al(0xE0); rec("OPL status after reset")
    opl(2, 0xFF); opl(4, 0x21)
    a.mov_r16_imm("cx", 200); a.mov_r16_imm("dx", 0x388); a.label("opl_d"); a.in_al_dx(); a.loop("opl_d")
    a.in_al(0x388); a.mov_r8_imm("ah", 0); a.and_al(0xE0); rec("OPL status after timer 1 (80us+)")
    opl(4, 0x60); opl(4, 0x80)

    # --- clocks -------------------------------------------------------------------
    # PIT channel 0 in mode 2 with the full count: one count per 1/1193182 s.
    a.cli()
    a.out_imm(0x43, 0x34); a.out_imm(0x40, 0); a.out_imm(0x40, 0)

    def latch(r):
        a.out_imm(0x43, 0x00); a.in_al(0x40); a.mov_rr8("bl" if r == "bx" else "cl", "al")
        a.in_al(0x40); a.mov_rr8("bh" if r == "bx" else "ch", "al")

    def wait_vr_start(tag):
        a.mov_r16_imm("dx", 0x3DA)
        a.label("vrend_" + tag); a.in_al_dx(); a.test_al(8); a.jcc("jnz", "vrend_" + tag)
        a.label("vrbeg_" + tag); a.in_al_dx(); a.test_al(8); a.jcc("jz", "vrbeg_" + tag)

    def wait_vr_end(tag):
        a.mov_r16_imm("dx", 0x3DA)
        a.label("vrin_" + tag); a.in_al_dx(); a.test_al(8); a.jcc("jnz", "vrin_" + tag)

    # VGA frame period in PIT counts, three times
    for k in range(3):
        wait_vr_start("p%da" % k); latch("bx")
        wait_vr_start("p%db" % k); latch("cx")
        a.sub_rr16("bx", "cx"); rec("VGA frame period in PIT counts #%d" % k, "bx", "rate")
    # retrace length
    for k in range(2):
        wait_vr_start("r%d" % k); latch("bx"); wait_vr_end("r%d" % k); latch("cx")
        a.sub_rr16("bx", "cx"); rec("VGA retrace length in PIT counts #%d" % k, "bx", "rate")
    # status polls per frame: what a port read costs
    for k in range(2):
        wait_vr_start("q%da" % k)
        a.xor_rr16("si", "si")
        a.label("q%d_out" % k); a.inc16("si"); a.in_al_dx(); a.test_al(8); a.jcc("jnz", "q%d_out" % k)
        a.label("q%d_in" % k); a.inc16("si"); a.in_al_dx(); a.test_al(8); a.jcc("jz", "q%d_in" % k)
        rec("3DA polls per VGA frame #%d" % k, "si", "rate")
    # instructions per PIT count: 20000 iterations of LOOP
    for k in range(2):
        latch("bx"); a.mov_r16_imm("cx", 20000); a.label("ipc%d" % k); a.loop("ipc%d" % k)
        latch("cx"); a.sub_rr16("bx", "cx"); rec("PIT counts for 20000 LOOPs #%d" % k, "bx", "rate")
    # an OUT to an unused port
    for k in range(2):
        latch("bx"); a.mov_r16_imm("si", 2000); a.mov_r16_imm("dx", 0x0080); a.label("outc%d" % k)
        a.out_dx_al(); a.dec16("si"); a.jcc("jnz", "outc%d" % k)
        latch("cx"); a.sub_rr16("bx", "cx"); rec("PIT counts for 2000 OUTs #%d" % k, "bx", "rate")
    # BIOS tick: PIT back to mode 3, the BIOS rate, then count ticks over frames
    a.out_imm(0x43, 0x36); a.out_imm(0x40, 0); a.out_imm(0x40, 0)
    a.sti()
    a.xor_rr16("ax", "ax"); a.mov_sreg_r16("es", "ax")
    a.load_es_abs("bx", 0x46C)
    a.mov_r16_imm("si", 140)
    for tag in ("t",):
        a.label("tick_f")
        wait_vr_start("tk")
        a.dec16("si"); a.jcc("jnz", "tick_f")
    a.load_es_abs("cx", 0x46C); a.sub_rr16("cx", "bx"); rec("BIOS ticks over 140 VGA frames", "cx", "rate")
    a.mov_r16_sreg("ax", "cs"); a.mov_sreg_r16("es", "ax")

    # --- what the services cost in time ---------------------------------------------
    # PIT channel 2, gated on with the speaker off, counts down from 65536 in
    # mode 2 and is read by latching: an independent clock that the timer
    # interrupt does not touch. Each measurement repeats a call and reads the
    # counts it took. The counts include the loop around the call (a DEC and
    # a JNZ), the same on both machines.
    a.in_al(0x61); a.and_al(0xFC); a.db(0x0C, 0x01); a.db(0xE6, 0x61)   # gate on, speaker off
    a.out_imm(0x43, 0xB4); a.out_imm(0x42, 0); a.out_imm(0x42, 0)

    def latch2(lo, hi):
        a.out_imm(0x43, 0x80); a.in_al(0x42); a.mov_rr8(lo, "al"); a.in_al(0x42); a.mov_rr8(hi, "al")

    def timed(name, n, body, tag, kind="rate"):
        """PIT counts for n repetitions of body (which may use AX, CX, DX)."""
        a.push("bx"); a.push("si")
        latch2("bl", "bh")
        a.store("t0", "bx")                  # the start, where no service can touch it
        a.mov_r16_imm("si", n)
        a.label("cost_" + tag)
        a.push("si"); body(); a.pop("si")
        a.dec16("si"); a.jcc("jnz", "cost_" + tag)
        latch2("cl", "ch")
        a.load("bx", "t0")
        a.sub_rr16("bx", "cx")
        rec(name, "bx", kind)
        a.pop("si"); a.pop("bx")
        a.mov_r16_sreg("ax", "cs"); a.mov_sreg_r16("es", "ax")   # a service may have changed ES

    def svc(vec, ax, extra=None):
        def body():
            a.mov_r16_imm("ax", ax)
            if extra:
                extra()
            a.int_(vec)
        return body

    timed("PIT counts: 200 x INT 21h/0Bh (stdin status)", 200, svc(0x21, 0x0B00), "21_0b")
    timed("PIT counts: 200 x INT 21h/30h (version)", 200, svc(0x21, 0x3000), "21_30")
    timed("PIT counts: 200 x INT 21h/2Ch (time)", 200, svc(0x21, 0x2C00), "21_2c")
    timed("PIT counts: 200 x INT 21h/35h (get vector)", 200, svc(0x21, 0x3508), "21_35")
    timed("PIT counts: 200 x INT 21h/19h (current drive)", 200, svc(0x21, 0x1900), "21_19")
    timed("PIT counts: 200 x INT 21h/62h (get PSP)", 200, svc(0x21, 0x6200), "21_62")
    timed("PIT counts: 200 x INT 21h/51h (get PSP)", 200, svc(0x21, 0x5100), "21_51")
    timed("PIT counts: 200 x INT 21h/0Bh with IF off", 200, lambda: (a.cli(), svc(0x21, 0x0B00)(), a.sti()), "21_0b_cli")
    timed("PIT counts: 200 x CALL FAR 0060:0008-style IRET (baseline)", 200, lambda: (a.db(0x9C), a.db(0x0E), a.db(0xE8, 0x00, 0x00), a.db(0x58), a.db(0x58), a.db(0x9D)), "baseline")
    timed("PIT counts: 200 x INT 16h/01h (key status)", 200, svc(0x16, 0x0100), "16_01")
    timed("PIT counts: 200 x INT 10h/0Fh (video mode)", 200, svc(0x10, 0x0F00), "10_0f")
    timed("PIT counts: 200 x INT 1Ah/00h (ticks)", 200, svc(0x1A, 0x0000), "1a_00")
    timed("PIT counts: 200 x INT 33h/03h (mouse)", 200, svc(0x33, 0x0003), "33_03")
    timed("PIT counts: 200 x INT 11h (equipment)", 200, svc(0x11, 0x0000), "11")
    # a file read: open this program, read 4 KB twenty times from the start
    a.mov_r16_imm("ax", 0x3D00); a.mov_r16_label("dx", "s_self"); a.int_(0x21); a.store("handle", "ax")

    def read4k():
        a.load("bx", "handle"); a.mov_r16_imm("ax", 0x4200); a.xor_rr16("cx", "cx"); a.xor_rr16("dx", "dx"); a.int_(0x21)
        a.load("bx", "handle"); a.mov_r16_imm("ax", 0x3F00); a.mov_r16_imm("cx", 4096); a.mov_r16_label("dx", "lowcopy"); a.int_(0x21)
    # A read costs the rest of DOSBox's current CPU slice, so the total
    # depends on where in its event schedule each read lands: within 5%.
    timed("PIT counts: 20 x (seek + read 4 KB)", 20, read4k, "rd4k", "rate5")

    def seek_only():
        a.load("bx", "handle"); a.mov_r16_imm("ax", 0x4200); a.xor_rr16("cx", "cx"); a.xor_rr16("dx", "dx"); a.int_(0x21)
    timed("PIT counts: 20 x seek", 20, seek_only, "seek")
    a.load("bx", "handle"); a.mov_r16_imm("ax", 0x3E00); a.int_(0x21)
    # the timer interrupt's own cost: 20000 LOOPs with IRQ 0 at about 10 kHz,
    # then with interrupts off; the difference is the handlers' time
    def loops():
        a.mov_r16_imm("cx", 20000); a.label("lp_%d" % slot[0]); a.loop("lp_%d" % slot[0])
    a.cli(); a.out_imm(0x43, 0x34); a.out_imm(0x40, 119); a.out_imm(0x40, 0); a.sti()
    timed("PIT counts: 20000 LOOPs, IRQ 0 at 10 kHz", 1, loops, "irq_on")
    a.cli()
    timed("PIT counts: 20000 LOOPs, interrupts off", 1, loops, "irq_off")
    a.out_imm(0x43, 0x36); a.out_imm(0x40, 0); a.out_imm(0x40, 0); a.sti()
    a.in_al(0x61); a.and_al(0xFC); a.db(0xE6, 0x61)

    # --- every service the inventory lists, every register after it ---------------
    call("21/30 AL=0", 0x21, {"ax": 0x3000})
    call("21/30 AL=AE", 0x21, {"ax": 0x30AE})
    call("21/3306", 0x21, {"ax": 0x3306})
    call("21/3300", 0x21, {"ax": 0x3300})
    call("21/0B", 0x21, {"ax": 0x0B00})
    call("21/09", 0x21, {"ax": 0x0900, "dx": "s_dollar"})
    call("21/58 get", 0x21, {"ax": 0x5800})
    call("21/51", 0x21, {"ax": 0x5100})
    call("21/62", 0x21, {"ax": 0x6200})
    call("21/4D", 0x21, {"ax": 0x4D00})
    call("21/35 08", 0x21, {"ax": 0x3508}, skip=("BX", "ES"))
    call("21/1A set DTA", 0x21, {"ax": 0x1A00, "dx": "dta"})
    call("21/11 FCB find F117.COM", 0x21, {"ax": 0x1100, "dx": "fcb"})
    for k in range(0, 0x18, 2):
        # the file's date and time (+14h, +16h) are the host's in DOSBox
        a.load("ax", "dta_%d" % k); rec("21/11 DTA +%02X" % k, kind="ignore" if k in (0x14, 0x16) else "exact")
    call("21/43 attributes F117.COM", 0x21, {"ax": 0x4300, "dx": "s_self"})
    call("21/43 attributes missing", 0x21, {"ax": 0x4300, "dx": "s_none"})
    call("21/3D open missing", 0x21, {"ax": 0x3D00, "dx": "s_none"})
    call("21/3D open F117.COM", 0x21, {"ax": 0x3D00, "dx": "s_self"}, keep="handle")
    a.load("bx", "handle"); a.mov_r16_imm("ax", 0x4400); a.int_(0x21); snapshot("21/4400 on a file", skip=("BX",))
    a.load("bx", "handle"); a.mov_r16_imm("ax", 0x3F00); a.mov_r16_imm("cx", 16); a.mov_r16_label("dx", "rbuf")
    a.int_(0x21); snapshot("21/3F read 16", skip=("BX",))
    a.load("ax", "rbuf"); rec("21/3F first word read")
    for mode, (hi, lo) in (("set 2", (0, 2)), ("cur 0", (0, 0)), ("end 0", (0, 0))):
        a.load("bx", "handle"); a.mov_r16_imm("ax", 0x4200 | {"set": 0, "cur": 1, "end": 2}[mode.split()[0]])
        a.mov_r16_imm("cx", hi); a.mov_r16_imm("dx", lo); a.int_(0x21); snapshot("21/42 seek " + mode, skip=("BX",))
    a.load("bx", "handle"); a.mov_r16_imm("ax", 0x3E00); a.int_(0x21); snapshot("21/3E close", skip=("BX",))
    call("21/3E close bad handle", 0x21, {"ax": 0x3E00, "bx": 0x0063})
    call("21/48 alloc 16", 0x21, {"ax": 0x4800, "bx": 0x0010}, keep="blk")
    a.load("ax", "blk"); a.mov_sreg_r16("es", "ax"); a.mov_r16_imm("ax", 0x4A00); a.mov_r16_imm("bx", 8)
    a.int_(0x21); snapshot("21/4A shrink to 8")
    a.load("ax", "blk"); a.mov_sreg_r16("es", "ax"); a.mov_r16_imm("ax", 0x4A00); a.mov_r16_imm("bx", 0x7FFF)
    a.int_(0x21); snapshot("21/4A grow too far")
    a.load("ax", "blk"); a.mov_sreg_r16("es", "ax"); a.mov_r16_imm("ax", 0x4900)
    a.int_(0x21); snapshot("21/49 free")
    call("21/48 largest", 0x21, {"ax": 0x4800, "bx": 0xFFFF})
    call("21/49 free bad", 0x21, {"ax": 0x4900}, es=0x0050)
    call("21/2D set time", 0x21, {"ax": 0x2D00, "cx": 0x0C1E, "dx": 0x0000})
    call("21/2D set bad time", 0x21, {"ax": 0x2D00, "cx": 0x1E1E, "dx": 0x0000})
    call("21/2C get time", 0x21, {"ax": 0x2C00}, skip=("CX", "DX"))
    call("21/2A get date", 0x21, {"ax": 0x2A00}, skip=("AX", "CX", "DX"))
    call("10/0F", 0x10, {"ax": 0x0F00})
    call("10/02 set cursor", 0x10, {"ax": 0x0200, "bx": 0x0000, "dx": 0x0A05})
    call("10/03 get cursor", 0x10, {"ax": 0x0300, "bx": 0x0000})
    call("10/0E teletype", 0x10, {"ax": 0x0E41, "bx": 0x0007})
    call("10/09 write char", 0x10, {"ax": 0x0942, "bx": 0x0007, "cx": 1})
    call("10/1A", 0x10, {"ax": 0x1A00})
    call("10/1012 set DAC block", 0x10, {"ax": 0x1012, "bx": 2, "cx": 1, "dx": "dacblk"})
    call("10/1015 read DAC 2", 0x10, {"ax": 0x1015, "bx": 2})
    call("10/00 mode 13", 0x10, {"ax": 0x0013})
    call("10/0F in 13h", 0x10, {"ax": 0x0F00})
    call("10/00 mode 3", 0x10, {"ax": 0x0003})
    call("16/01 empty", 0x16, {"ax": 0x0100})
    call("16/02", 0x16, {"ax": 0x0200})
    call("1A/00", 0x1A, {"ax": 0x0000}, skip=("CX", "DX"))
    call("1A/02", 0x1A, {"ax": 0x0200}, skip=("CX", "DX"))
    call("33/00", 0x33, {"ax": 0x0000})
    call("33/03", 0x33, {"ax": 0x0003})
    call("33/04", 0x33, {"ax": 0x0004, "cx": 100, "dx": 50})
    call("33/03 after 04", 0x33, {"ax": 0x0003})
    call("33/07", 0x33, {"ax": 0x0007, "cx": 0, "dx": 639})
    call("33/08", 0x33, {"ax": 0x0008, "cx": 0, "dx": 199})
    call("33/0F", 0x33, {"ax": 0x000F, "cx": 8, "dx": 16})
    call("11", 0x11, {})
    call("12", 0x12, {})

    # --- EXEC a child (CHILD.EXE, all memory wanted) and come back ----------------
    # The parameter block's segments are the probe's own: filled in here.
    a.mov_r16_sreg("ax", "cs")
    for off in (4, 8, 12):
        a.db(0x2E, 0xA3); a.ref16("pblock_%d" % off)        # mov cs:[pblock+off], ax
    call("21/4B00 EXEC child", 0x21, {"ax": 0x4B00, "dx": "s_child", "bx": "pblock"})
    call("21/4D after child", 0x21, {"ax": 0x4D00})
    call("21/48 largest after child", 0x21, {"ax": 0x4800, "bx": 0xFFFF})

    # --- write the answers, the PSP and the environment --------------------------
    a.mov_r16_imm("ax", 0x3C00); a.xor_rr16("cx", "cx"); a.mov_r16_label("dx", "fname"); a.int_(0x21)
    a.mov_rr16("bx", "ax")
    a.mov_r16_imm("ax", 0x4000); a.mov_r16_label("cx", "nres2"); a.mov_r16_label("dx", "res_0"); a.int_(0x21)
    a.mov_r16_imm("ax", 0x4000); a.mov_r16_imm("cx", MCB_BYTES); a.mov_r16_label("dx", "mcbbuf"); a.int_(0x21)
    a.mov_r16_imm("ax", 0x4000); a.mov_r16_imm("cx", 0x100); a.mov_r16_imm("dx", 0); a.int_(0x21)     # PSP
    a.push("ds"); a.db(0x8E, 0x1E); a.dw(0x2C)                                     # mov ds, [2Ch]
    a.mov_r16_imm("ax", 0x4000); a.mov_r16_imm("cx", 0x180); a.mov_r16_imm("dx", 0); a.int_(0x21)     # environment
    a.pop("ds")
    # raw memory as the program found it at entry, then as it is now:
    # the low 6.4 KB (vectors, BIOS data, DOS area, MCBs) was copied at
    # entry into lowcopy; the top-of-memory and UMB control blocks now.
    a.mov_r16_imm("ax", 0x4000); a.mov_r16_imm("cx", LOW_BYTES); a.mov_r16_label("dx", "lowcopy"); a.int_(0x21)
    for seg in (0x9FFF, 0xD000):
        a.push("ds"); a.mov_r16_imm("ax", seg); a.mov_sreg_r16("ds", "ax")
        a.mov_r16_imm("ax", 0x4000); a.mov_r16_imm("cx", 16); a.mov_r16_imm("dx", 0); a.int_(0x21)
        a.pop("ds")
    a.mov_r16_imm("ax", 0x3E00); a.int_(0x21)
    a.mov_r16_imm("ax", 0x0003); a.int_(0x10)
    a.mov_r16_imm("ax", 0x4C00); a.int_(0x21)
    a.label("fname"); a.db(b"OUT.BIN\0")
    a.label("s_dollar"); a.db(b"ok$")
    a.label("s_self"); a.db(b"F117.COM\0")
    a.label("s_none"); a.db(b"NOFILE.XYZ\0")
    a.label("s_child"); a.db(b"CHILD.EXE\0")
    a.label("ctail"); a.db(b"\x03 /X\r"); a.db(bytes(128 - 5))     # DOS copies all 128 bytes
    if len(a.b) & 1:
        a.db(0)
    a.label("fcba"); a.db(0); a.db(b"ALPHA   TXT"); a.db(bytes(4))
    a.label("fcbb"); a.db(0); a.db(b"BETA    DAT"); a.db(bytes(4))
    a.label("pblock")
    a.dw(0)                                           # environment: the parent's
    a.ref16("ctail"); a.labels["pblock_4"] = a.here; a.dw(0)
    a.ref16("fcba"); a.labels["pblock_8"] = a.here; a.dw(0)
    a.ref16("fcbb"); a.labels["pblock_12"] = a.here; a.dw(0)
    a.label("dacblk"); a.db(0x11, 0x22, 0x33)
    a.label("cursor_masks")
    for _ in range(16): a.dw(0xFFFF)
    a.dw(0x8000)
    for _ in range(15): a.dw(0)
    if len(a.b) & 1:
        a.db(0)
    a.label("fcb"); a.db(0); a.db(b"F117    COM"); a.db(bytes(25))
    if len(a.b) & 1:
        a.db(0)
    for k in range(0, 0x80, 2):
        a.labels["dta_%d" % k] = a.here + k
    a.label("dta"); a.db(bytes(0x80))
    a.label("rbuf"); a.db(bytes(16))
    a.label("handle"); a.dw(0)
    a.label("blk"); a.dw(0)
    a.label("t0"); a.dw(0)
    a.label("lowcopy"); a.db(bytes(LOW_BYTES))
    if len(a.b) & 1:
        a.db(0)
    for k in range(slot[0]):
        a.label("res_%d" % k); a.dw(0xEEEE)
    a.labels["nres2"] = 2 * slot[0]
    a.labels["psp_env"] = 0x2C
    a.label("mcbbuf"); a.db(bytes(MCB_BYTES))
    return a.link(), slot[0]


CHILD_FIELDS = []    # the EXEC'd child's answers


def build_child():
    """CHILD.EXE: an MZ program the probe EXECs, asking for all memory as the
    game's programs do. It records the registers it starts with, the memory
    control block chain it sees, its PSP and its environment, writes them to
    CHILD.BIN and exits with code 2Ah."""
    a = Asm(origin=0)
    slot = [0]

    def rec(name, r="ax"):
        lab = "res_%d" % slot[0]
        slot[0] += 1
        CHILD_FIELDS.append((name, "exact"))
        a.store(lab, r)

    for r in ("ax", "bx", "cx", "dx", "si", "di", "bp", "sp"):
        rec("child entry %s" % r.upper(), r)
    a.pushf_pop("ax"); a.db(0x25); a.dw(0x0FD5 | 0x0200 | 0x0400); rec("child entry FLAGS")
    for s in ("ds", "es", "ss"):
        a.mov_r16_sreg("ax", s); rec("child entry %s" % s.upper())
    a.mov_r16_sreg("ax", "cs"); rec("child entry CS")
    a.mov_r16_sreg("ax", "ds"); a.store("psp", "ax")
    # the MCB chain
    a.mov_r16_sreg("ax", "cs"); a.mov_sreg_r16("ds", "ax")
    a.mov_r16_imm("ax", 0x5200); a.int_(0x21)
    a.db(0x26, 0x8B, 0x47, 0xFE)                     # mov ax, es:[bx-2]
    a.mov_r16_label("di", "mcbbuf")
    a.mov_r16_imm("bp", 24)
    a.label("mcb_l")
    a.mov_sreg_r16("es", "ax")
    a.db(0x2E, 0x89, 0x05)                           # mov cs:[di], ax
    for k in range(8):
        a.load_es_abs("dx", 2 * k)
        a.db(0x2E, 0x89, 0x55, 2 + 2 * k)            # mov cs:[di+2+2k], dx
    a.add_r16_imm("di", 18)
    a.load_es_abs("dx", 3); a.add_r16_imm("ax", 1); a.db(0x01, 0xD0)
    a.load_es_abs8("dl", 0); a.db(0x80, 0xFA, ord("Z")); a.jcc("je", "mcb_done")
    a.dec16("bp"); a.jcc("jnz", "mcb_l")
    a.label("mcb_done")
    # CHILD.BIN: answers, MCBs, PSP, environment
    a.mov_r16_sreg("ax", "cs"); a.mov_sreg_r16("ds", "ax")
    a.mov_r16_imm("ax", 0x3C00); a.xor_rr16("cx", "cx"); a.mov_r16_label("dx", "fname"); a.int_(0x21)
    a.mov_rr16("bx", "ax")
    a.mov_r16_imm("ax", 0x4000); a.mov_r16_label("cx", "nres2"); a.mov_r16_label("dx", "res_0"); a.int_(0x21)
    a.mov_r16_imm("ax", 0x4000); a.mov_r16_imm("cx", MCB_BYTES); a.mov_r16_label("dx", "mcbbuf"); a.int_(0x21)
    a.load("ax", "psp"); a.mov_sreg_r16("ds", "ax")
    a.mov_r16_imm("ax", 0x4000); a.mov_r16_imm("cx", 0x100); a.mov_r16_imm("dx", 0); a.int_(0x21)
    a.db(0x8E, 0x1E); a.dw(0x2C)                     # mov ds, [2Ch]
    a.mov_r16_imm("ax", 0x4000); a.mov_r16_imm("cx", 0x180); a.mov_r16_imm("dx", 0); a.int_(0x21)
    a.mov_r16_sreg("ax", "cs"); a.mov_sreg_r16("ds", "ax")
    a.mov_r16_imm("ax", 0x3E00); a.int_(0x21)
    a.mov_r16_imm("ax", 0x4C2A); a.int_(0x21)
    a.label("fname"); a.db(b"CHILD.BIN\0")
    if len(a.b) & 1:
        a.db(0)
    a.label("psp"); a.dw(0)
    for k in range(slot[0]):
        a.label("res_%d" % k); a.dw(0xEEEE)
    a.labels["nres2"] = 2 * slot[0]
    a.label("mcbbuf"); a.db(bytes(MCB_BYTES))
    image = a.link()
    # The header: 32 bytes, no relocations, a 1 KB stack after the image,
    # all memory wanted (maximum allocation FFFFh), like the game's programs.
    image_paras = (len(image) + 15) // 16
    image = image + bytes(image_paras * 16 - len(image))
    total = 32 + len(image)
    hdr = struct.pack("<2s13H", b"MZ", total % 512, (total + 511) // 512, 0, 2, 0x40, 0xFFFF,
                      image_paras, 0x400, 0, 0, 0, 0x1C, 0)
    hdr = hdr + bytes(32 - len(hdr))
    return hdr + image, slot[0]


R16["ds"] = None   # push/pop ds handled below


def _push_ds(self, r):
    if r == "ds":
        self.db(0x1E)
    else:
        self.db(0x50 + R16[r])


def _pop_ds(self, r):
    if r == "ds":
        self.db(0x1F)
    else:
        self.db(0x58 + R16[r])


Asm.push = _push_ds
Asm.pop = _pop_ds


# ---------------------------------------------------------------------------
# Running it.
# ---------------------------------------------------------------------------
def run_dosbox(dosbox, data, probe, work, child, tag="dosbox"):
    d = os.path.join(work, tag)
    os.makedirs(d, exist_ok=True)
    open(os.path.join(d, "F117.COM"), "wb").write(probe)
    open(os.path.join(d, "CHILD.EXE"), "wb").write(child)
    conf = os.path.join(work, tag + "-probe.conf")
    with open(conf, "w") as f:
        f.write("[sdl]\nfullscreen=false\noutput=surface\n[autoexec]\n@echo off\n")
        f.write('mount C "%s"\nc:\nkeyb us\ncls\nf117\nexit\n' % d)
    env = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
    base = os.path.join(data, "dosboxF117A.conf")
    extra = ["-nopromptfolder", "-fastlaunch"] if "dosbox-x" in os.path.basename(dosbox).lower() else []
    subprocess.run([dosbox, "-conf", base, "-conf", conf, "-noconsole"] + extra, cwd=os.path.dirname(dosbox),
                   env=env, timeout=180)
    return open(os.path.join(d, "OUT.BIN"), "rb").read(), open(os.path.join(d, "CHILD.BIN"), "rb").read()


def run_ours(probe, work, child, engine="interp"):
    d = os.path.join(work, "ours")
    os.makedirs(d, exist_ok=True)
    open(os.path.join(d, "F117.COM"), "wb").write(probe)
    open(os.path.join(d, "CHILD.EXE"), "wb").write(child)
    save = os.path.join(work, "ours_save")
    exe = os.path.join(ROOT, "build", "f117run.exe")
    subprocess.run([exe, "--engine", engine, "--data", d, "--save", save, "--steps", "400000000",
                    "--log", os.path.join(work, "ours.log")], capture_output=True, text=True, timeout=600)
    for base in (save, d):
        p = os.path.join(base, "OUT.BIN")
        if os.path.exists(p):
            return open(p, "rb").read(), open(os.path.join(base, "CHILD.BIN"), "rb").read()
    sys.exit("f117run wrote no OUT.BIN (see %s)" % os.path.join(work, "ours.log"))


def env_strings(block):
    out, cur = [], b""
    i = 0
    while i < len(block):
        if block[i] == 0:
            if not cur:
                break
            out.append(cur.decode("latin-1"))
            cur = b""
        else:
            cur += bytes([block[i]])
        i += 1
    prog = ""
    if i + 3 < len(block):
        n = block[i + 1] | (block[i + 2] << 8)
        prog = block[i + 3:].split(b"\0")[0].decode("latin-1")
        out.append("(strings after the environment: %d, program %s)" % (n, prog))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--data", required=True, help="the game's install (holds DOSBOX and dosboxF117A.conf)")
    ap.add_argument("--dosbox", default=None, help="the reference DOSBox (default: GOG's, in the install)")
    ap.add_argument("--reference", choices=("gog", "dosbox-x"), default="gog",
                    help="dosbox-x: DOSBox-X (the second reference) with GOG's settings")
    ap.add_argument("--diffs-only", action="store_true")
    ap.add_argument("--work", default=os.path.join(os.path.expanduser("~"), "f117-recomp-local", "fidelity"))
    a = ap.parse_args()
    tag = "dosbox" if a.reference == "gog" else "dosbox-x"
    dosbox = a.dosbox or (os.path.join(a.data, "DOSBOX", "DOSBox.exe") if a.reference == "gog" else
                          os.path.join(os.path.expanduser("~"), "f117-recomp-local", "dosbox-x", "dosbox-x.exe"))
    probe, n = build_probe()
    child, nc = build_child()
    os.makedirs(a.work, exist_ok=True)
    print("probe: %d bytes, %d answers; child: %d bytes, %d answers" % (len(probe), n, len(child), nc))
    ref, ref_child = run_dosbox(dosbox, a.data, probe, a.work, child, tag)
    ours, ours_child = run_ours(probe, a.work, child)
    open(os.path.join(a.work, tag + ".bin"), "wb").write(ref)
    open(os.path.join(a.work, "ours.bin"), "wb").write(ours)
    print("answer sheets: %s" % os.path.join(a.work, "{dosbox,ours}.bin"))
    same = differ = 0
    for k, (name, kind) in enumerate(FIELDS):
        r = struct.unpack_from("<H", ref, 2 * k)[0]
        o = struct.unpack_from("<H", ours, 2 * k)[0]
        if kind == "rate":
            ok = abs(r - o) <= max(2, r // 50)          # within 2%
        elif kind == "rate5":
            ok = abs(r - o) <= max(2, r // 20)          # within 5%
        elif kind == "nonzero":
            ok = (r != 0) == (o != 0)
        elif kind == "zeroish":
            ok = abs((r ^ 0x8000) - (o ^ 0x8000)) <= 2
        elif kind == "ignore":
            continue
        else:
            ok = r == o
        same += ok
        differ += not ok
        if ok and a.diffs_only:
            continue
        print("  %-4s %-48s DOSBox %04X (%5d)  ours %04X (%5d)" % ("" if ok else "DIFF", name, r, r, o, o))
    print("memory control blocks (segment: type owner size name), DOSBox | ours:")
    for k in range(24):
        rows = []
        for sheet in (ref, ours):
            e = sheet[2 * n + 18 * k: 2 * n + 18 * k + 18]
            seg, sig, owner, size = struct.unpack_from("<HBHH", e, 0)
            if sig not in (0x4D, 0x5A):
                rows.append("-")
                continue
            name = e[2 + 8:2 + 16].split(b"\x00")[0].decode("latin-1", "replace")
            rows.append("%04X: %s owner %04X size %04X %-8s" % (seg, chr(sig), owner, size, name))
        if rows == ["-", "-"]:
            break
        print("   %-44s | %s" % tuple(rows))
    off = 2 * n + MCB_BYTES
    print("PSP bytes that differ (offset: DOSBox ours):")
    for i in range(0x100):
        if ref[off + i] != ours[off + i] and not (0x2C <= i < 0x2E):
            print("   %02X: %02X %02X" % (i, ref[off + i], ours[off + i]))
    print("environment, DOSBox:", env_strings(ref[off + 0x100:]))
    print("environment, ours:  ", env_strings(ours[off + 0x100:]))
    # the EXEC'd child
    print("the EXEC'd child:")
    for k, (name, kind) in enumerate(CHILD_FIELDS):
        r = struct.unpack_from("<H", ref_child, 2 * k)[0]
        o = struct.unpack_from("<H", ours_child, 2 * k)[0]
        ok = r == o
        same += ok
        differ += not ok
        if ok and a.diffs_only:
            continue
        print("  %-4s %-48s DOSBox %04X (%5d)  ours %04X (%5d)" % ("" if ok else "DIFF", name, r, r, o, o))
    print("  child's memory control blocks, DOSBox | ours:")
    for k in range(24):
        rows = []
        for sheet in (ref_child, ours_child):
            e = sheet[2 * nc + 18 * k: 2 * nc + 18 * k + 18]
            seg, sig, owner, size = struct.unpack_from("<HBHH", e, 0)
            if sig not in (0x4D, 0x5A):
                rows.append("-")
                continue
            nm = e[2 + 8:2 + 16].split(b"\x00")[0].decode("latin-1", "replace")
            rows.append("%04X: %s owner %04X size %04X %-8s" % (seg, chr(sig), owner, size, nm))
        if rows == ["-", "-"]:
            break
        same += rows[0] == rows[1]
        differ += rows[0] != rows[1]
        print("   %s %-44s | %s" % ("    " if rows[0] == rows[1] else "DIFF", rows[0], rows[1]))
    co = 2 * nc + MCB_BYTES
    for i in range(0x100):
        ok = ref_child[co + i] == ours_child[co + i]
        same += ok
        differ += not ok
        if not ok:
            print("   DIFF child PSP %02X: DOSBox %02X ours %02X" % (i, ref_child[co + i], ours_child[co + i]))
    print("  child environment, DOSBox:", env_strings(ref_child[co + 0x100:]))
    print("  child environment, ours:  ", env_strings(ours_child[co + 0x100:]))
    print("%d answers agree, %d differ" % (same, differ))
    return int(differ != 0)


if __name__ == "__main__":
    raise SystemExit(main())
