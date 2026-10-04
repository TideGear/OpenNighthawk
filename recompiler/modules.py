"""modules.py - the game's code files as the recompiler sees them.

Each module is the image the CPU will execute, before relocation: the
unpacked load module for an LZEXE program (recovered by running the
program's own decompressor under the validated core, tools/unpack.py), the
load module of a plain MZ overlay or driver, or the bytes of a .COM. With it
come the relocation sites (whose words the loader patches, so the
translation reads them from memory), the entry points the file itself
declares, and the hash of the file as DOS reads it - which is how the
run-time recognises the module when DOS loads it.
"""
from __future__ import annotations

import os
import struct
import sys
from dataclasses import dataclass, field

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(os.path.dirname(HERE), "tools"))

# Every file of the DOS release that holds code.
CODE_FILES = [
    "F117.COM", "SETUP.EXE", "MPS_LOGO.EXE", "PLAYER.EXE", "DSWAP.EXE",
    "START.EXE", "VGAME.EXE", "END.EXE",
    "MGRAPHIC.EXE", "MISC.EXE",
    "ASOUND.117", "ISOUND.117", "RSOUND.117", "NSOUND.117",
    "ASOUND.LOG", "ISOUND.LOG", "RSOUND.LOG",
]


def fnv1a64(data: bytes) -> int:
    h = 0xCBF29CE484222325
    for b in data:
        h ^= b
        h = (h * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return h


@dataclass
class Module:
    name: str
    file_hash: int
    kind: str                  # "exe", "com", "overlay"
    image: bytes
    relocs: set                # image offsets of relocated words
    origin: int = 0            # 0x100 for a .COM
    entries: list = field(default_factory=list)   # (seg, ip) declared entry points
    stack_seg: int = -1        # SS from the header: DGROUP for Microsoft C
    packed: bool = False
    probe: tuple | None = None # floating modules: (offset, length) recognised at CS:offset

    def off(self, seg, ip):
        return seg * 16 + ip - self.origin

    def reloc_bytes(self):
        s = set()
        for r in self.relocs:
            s.add(r)
            s.add(r + 1)
        return s


def _find_ci(directory, name):
    p = os.path.join(directory, name)
    if os.path.exists(p):
        return p
    low = name.lower()
    for f in os.listdir(directory):
        if f.lower() == low:
            return os.path.join(directory, f)
    return None


def _mz(data):
    (_sig, last_page, pages, nreloc, hdr_para, minalloc, maxalloc,
     ss, sp, _cs, ip, cs, reloc_off, _ov) = struct.unpack("<14H", data[:28])
    size = (pages - 1) * 512 + (last_page or 512) if pages else 0
    size = min(size, len(data))
    return dict(nreloc=nreloc, hdr=hdr_para * 16, ss=ss, sp=sp, ip=ip, cs=cs,
                reloc_off=reloc_off, size=size)


def load_module(directory, name):
    path = _find_ci(directory, name)
    if not path:
        return None
    raw = open(path, "rb").read()
    h = fnv1a64(raw)
    uname = name.upper()
    if uname.endswith(".COM"):
        return Module(uname, h, "com", raw, set(), origin=0x100, entries=[(0, 0x100)])
    if raw[:2] not in (b"MZ", b"ZM"):
        return None
    hdr = _mz(raw)
    if raw[0x1C:0x20] in (b"LZ91", b"LZ09"):
        import unpack                      # tools/unpack.py, run on our core
        img_a, seg_a, info = unpack.load_and_run(raw, 0x1000, verbose=False)
        img_b, seg_b, _ = unpack.load_and_run(raw, 0x3000, verbose=False)
        relocs, anomalies = unpack.recover_relocs(img_a, seg_a, img_b, seg_b)
        if anomalies:
            print("  %s: %d non-relocation differences while unpacking" % (uname, len(anomalies)))
        img = bytearray(img_a)
        for r in relocs:
            w = (img[r] | (img[r + 1] << 8)) - seg_a
            img[r] = w & 0xFF
            img[r + 1] = (w >> 8) & 0xFF
        real_ip, real_cs, sp, ss = info[0], info[1], info[2], info[3]
        return Module(uname, h, "exe", bytes(img), set(relocs),
                      entries=[(real_cs, real_ip)], stack_seg=ss, packed=True)
    body = raw[hdr["hdr"]:hdr["size"]]
    relocs = set()
    for i in range(hdr["nreloc"]):
        o, s = struct.unpack("<HH", raw[hdr["reloc_off"] + i * 4: hdr["reloc_off"] + i * 4 + 4])
        relocs.add(s * 16 + o)
    m = Module(uname, h, "exe", body, relocs, stack_seg=hdr["ss"])
    # The MicroProse module descriptor of the overlays and drivers: a stamp,
    # the base paragraph (+18), the first entry index (+1C), the count (+22)
    # and the entry offsets (+24), relative to the base.
    if len(body) > 0x24:
        base, _w1a, first, _w1e, _w20, count = struct.unpack("<6H", body[0x18:0x24])
        stamp = body[:12]
        if 0 < count < 200 and base * 16 < len(body) and all(32 <= b < 127 for b in stamp):
            m.kind = "overlay"
            for k in range(count):
                (e,) = struct.unpack("<H", body[0x24 + 2 * k: 0x26 + 2 * k])
                if base * 16 + e < len(body):
                    m.entries.append((base, e))
            return m
    if hdr["cs"] or hdr["ip"]:
        m.entries.append((hdr["cs"], hdr["ip"]))
    if uname.endswith(".LOG") or uname.endswith(".117"):
        m.kind = "overlay"
    return m


LZEXE_STUB = "LZEXE091.STUB"


def lzexe_stub(directory, names):
    """The LZEXE 0.91 decompressor as a floating module. Every packed
    program carries the same code; it copies itself above the program and
    jumps to the copy, keeping its offsets, so it is recognised wherever it
    runs by its code from the entry point on. The first 14 bytes (the
    program's own entry point and sizes) and the relocation data after the
    code differ per program and are never executed."""
    for n in names:
        path = _find_ci(directory, n)
        if not path:
            continue
        raw = open(path, "rb").read()
        if raw[:2] != b"MZ" or raw[0x1C:0x20] != b"LZ91":
            continue
        hdr = _mz(raw)
        stub = raw[hdr["hdr"]:hdr["size"]][hdr["cs"] * 16:]
        entries = [(0, hdr["ip"])]
        # Its first phase ends `push seg ; push 002Bh ; retf` into the copy.
        if stub[0x26:0x2B] == b"\xB8\x2B\x00\x50\xCB":
            entries.append((0, 0x2B))
        return Module(LZEXE_STUB, 0, "floating", stub, set(), entries=entries,
                      probe=(hdr["ip"], 0x40))
    return None


def load_all(directory):
    mods = []
    for n in CODE_FILES:
        m = load_module(directory, n)
        if m:
            mods.append(m)
    stub = lzexe_stub(directory, CODE_FILES)
    if stub:
        mods.append(stub)
    return mods
