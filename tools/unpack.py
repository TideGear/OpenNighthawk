#!/usr/bin/env python3
"""Unpack LZEXE-compressed DOS executables by running their own stub.

Rather than reimplementing the packer's bit stream and hoping it is right,
this loads the packed executable into the validated 8086 oracle and lets the
original decompressor do the work, stopping the moment it reaches the
program's real entry point. Whatever comes out is what DOS would have had in
memory, by construction.

The relocation table is recovered the same way, without needing to know how
the packer stores it: load and run the image twice at two different segments
and compare. Any word that differs by exactly the segment delta is a
relocation site; anything else that differs indicates a problem and is
reported.

Usage:
  unpack.py <packed.exe> [-o out.exe] [--dump-bin out.bin] [--quiet]
"""
from __future__ import annotations

import argparse
import ctypes
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
DLL = os.environ.get('F117R_CPU_DLL') or os.path.join(os.path.dirname(HERE), 'build', 'f117cpu_api.dll')

STOP_NAMES = {0: 'none', 1: 'step-budget', 2: 'breakpoint', 3: 'hlt',
              4: 'exit', 5: 'fault', 6: 'watchdog'}
R = {'ax': 0, 'cx': 1, 'dx': 2, 'bx': 3, 'sp': 4, 'bp': 5, 'si': 6, 'di': 7,
     'es': 8, 'cs': 9, 'ss': 10, 'ds': 11, 'ip': 12, 'flags': 13}


class Oracle:
    def __init__(self):
        self.lib = ctypes.CDLL(DLL)
        L = self.lib
        L.orc_new.restype = ctypes.c_void_p
        L.orc_get_reg.argtypes = [ctypes.c_void_p, ctypes.c_int]
        L.orc_get_reg.restype = ctypes.c_uint32
        L.orc_set_reg.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_uint32]
        L.orc_write_block.argtypes = [ctypes.c_void_p, ctypes.c_uint32,
                                      ctypes.c_char_p, ctypes.c_uint32]
        L.orc_read_block.argtypes = [ctypes.c_void_p, ctypes.c_uint32,
                                     ctypes.c_char_p, ctypes.c_uint32]
        L.orc_write8.argtypes = [ctypes.c_void_p, ctypes.c_uint32, ctypes.c_uint8]
        L.orc_set_bp.argtypes = [ctypes.c_void_p, ctypes.c_uint32]
        L.orc_run_to_bp.argtypes = [ctypes.c_void_p, ctypes.c_uint64]
        L.orc_run_to_bp.restype = ctypes.c_int
        L.orc_icount.argtypes = [ctypes.c_void_p]
        L.orc_icount.restype = ctypes.c_uint64
        L.orc_free.argtypes = [ctypes.c_void_p]
        self.h = L.orc_new()

    def __del__(self):
        try:
            self.lib.orc_free(self.h)
        except Exception:
            pass

    def get(self, n):
        return self.lib.orc_get_reg(self.h, R[n])

    def set(self, n, v):
        self.lib.orc_set_reg(self.h, R[n], v & 0xFFFF)

    def write(self, addr, data):
        self.lib.orc_write_block(self.h, addr, data, len(data))

    def read(self, addr, n):
        buf = ctypes.create_string_buffer(n)
        self.lib.orc_read_block(self.h, addr, buf, n)
        return buf.raw

    def poke8(self, addr, v):
        self.lib.orc_write8(self.h, addr, v)

    def run_to(self, linear, max_steps=200_000_000):
        self.lib.orc_set_bp(self.h, linear)
        return self.lib.orc_run_to_bp(self.h, max_steps)

    @property
    def icount(self):
        return self.lib.orc_icount(self.h)


def parse_mz(data):
    if data[:2] not in (b'MZ', b'ZM'):
        raise SystemExit('not an MZ executable')
    (_sig, last_page, pages, nreloc, hdr_para, minalloc, maxalloc,
     ss, sp, _csum, ip, cs, reloc_off, _ov) = struct.unpack('<14H', data[:28])
    image = (pages - 1) * 512 + (last_page or 512) if pages else 0
    return dict(last_page=last_page, pages=pages, nreloc=nreloc,
                hdr_para=hdr_para, minalloc=minalloc, maxalloc=maxalloc,
                ss=ss, sp=sp, ip=ip, cs=cs, reloc_off=reloc_off,
                hdr_size=hdr_para * 16, image_size=image)


def load_and_run(data, psp_seg, verbose=True):
    """Load the packed EXE at psp_seg and run its stub to the real entry.

    Returns (unpacked_image_bytes, load_seg, info).
    """
    h = parse_mz(data)
    load_seg = psp_seg + 0x10
    body = data[h['hdr_size']:h['image_size']]

    o = Oracle()

    # Fill the interrupt vector table with pointers to a HLT so that any
    # interrupt the stub takes stops us loudly instead of running into zeros.
    hlt_seg, hlt_off = 0x0060, 0x0000
    o.poke8(hlt_seg * 16 + hlt_off, 0xF4)          # HLT
    for v in range(256):
        o.write(v * 4, struct.pack('<HH', hlt_off, hlt_seg))

    # Minimal PSP: INT 20h at offset 0, and the segment of the byte past the
    # program's memory at offset 2, which some stubs consult.
    psp = bytearray(0x100)
    psp[0:2] = b'\xCD\x20'
    struct.pack_into('<H', psp, 2, psp_seg + 0x1000)
    o.write(psp_seg * 16, bytes(psp))

    o.write(load_seg * 16, body)

    # Apply the header's own relocations (LZEXE files carry none, but this
    # keeps the loader honest for plain executables too).
    for i in range(h['nreloc']):
        off, seg = struct.unpack('<HH', data[h['reloc_off'] + i * 4:
                                             h['reloc_off'] + i * 4 + 4])
        a = (load_seg + seg) * 16 + off
        cur = struct.unpack('<H', o.read(a, 2))[0]
        o.write(a, struct.pack('<H', (cur + load_seg) & 0xFFFF))

    # The stub's info block records the program's true entry point.
    stub = h['cs'] * 16
    info = struct.unpack('<8H', body[stub:stub + 16])
    real_ip, real_cs = info[0], info[1]

    o.set('ds', psp_seg)
    o.set('es', psp_seg)
    o.set('ss', (load_seg + h['ss']) & 0xFFFF)
    o.set('sp', h['sp'])
    o.set('cs', (load_seg + h['cs']) & 0xFFFF)
    o.set('ip', h['ip'])

    target = (((load_seg + real_cs) & 0xFFFF) * 16 + real_ip) & 0xFFFFF
    rc = o.run_to(target)
    if rc != 2:   # 2 == STOP_BREAKPOINT
        raise SystemExit('stub did not reach the entry point: %s after %d '
                         'instructions (CS:IP=%04X:%04X)'
                         % (STOP_NAMES.get(rc, rc), o.icount,
                            o.get('cs'), o.get('ip')))

    size = (info[4] + info[5]) * 16
    img = o.read(load_seg * 16, size)
    if verbose:
        print('    ran %d instructions; entry reached at %04X:%04X'
              % (o.icount, o.get('cs'), o.get('ip')))
    return img, load_seg, info


def recover_relocs(img_a, seg_a, img_b, seg_b):
    """Words differing by exactly the segment delta are relocation sites."""
    delta = (seg_b - seg_a) & 0xFFFF
    relocs, anomalies = [], []
    n = min(len(img_a), len(img_b))
    i = 0
    while i + 1 < n:
        if img_a[i] != img_b[i] or img_a[i + 1] != img_b[i + 1]:
            wa = img_a[i] | (img_a[i + 1] << 8)
            wb = img_b[i] | (img_b[i + 1] << 8)
            if ((wa + delta) & 0xFFFF) == wb:
                relocs.append(i)
                i += 2
                continue
            anomalies.append((i, wa, wb))
        i += 1
    return relocs, anomalies


def build_exe(img, info, relocs, minalloc, maxalloc):
    nrel = len(relocs)
    hdr_para = max(2, (28 + nrel * 4 + 15) // 16)
    hdr_size = hdr_para * 16
    total = hdr_size + len(img)
    pages = (total + 511) // 512
    out = bytearray(hdr_size)
    struct.pack_into('<14H', out, 0,
                     0x5A4D, total % 512, pages, nrel, hdr_para,
                     minalloc, maxalloc,
                     info[3], info[2], 0, info[0], info[1], 28, 0)
    off = 28
    for lin in relocs:
        seg, o = divmod(lin, 16)
        # Express as (segment, offset) with the offset kept small.
        struct.pack_into('<HH', out, off, o, seg)
        off += 4
    return bytes(out) + img


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('exe')
    ap.add_argument('-o', '--out', help='write a plain unpacked MZ here')
    ap.add_argument('--dump-bin', help='write the raw load module here')
    ap.add_argument('--quiet', action='store_true')
    a = ap.parse_args()

    with open(a.exe, 'rb') as f:
        data = f.read()
    name = os.path.basename(a.exe)
    v = not a.quiet
    if v:
        print('%s  (%d bytes packed)' % (name, len(data)))

    img_a, seg_a, info = load_and_run(data, 0x1000, v)
    img_b, seg_b, _ = load_and_run(data, 0x3000, v)

    relocs, anomalies = recover_relocs(img_a, seg_a, img_b, seg_b)

    # img_a still holds segment values relocated for seg_a. Subtract the load
    # base at every relocation site so the image is base-relative, the way it
    # sat in the original unpacked executable. Without this, every far
    # pointer in the dump is offset by the load segment and static analysis
    # resolves to the wrong place.
    img0 = bytearray(img_a)
    for lin in relocs:
        w = img0[lin] | (img0[lin + 1] << 8)
        w = (w - seg_a) & 0xFFFF
        img0[lin] = w & 0xFF
        img0[lin + 1] = w >> 8
    img_a = bytes(img0)

    print('  unpacked %d bytes; %d relocation sites recovered and rebased'
          % (len(img_a), len(relocs)))
    if anomalies:
        print('  WARNING: %d differing words that are not relocations '
              '(first few: %s)'
              % (len(anomalies),
                 ', '.join('%05X:%04X/%04X' % t for t in anomalies[:5])))

    if a.dump_bin:
        with open(a.dump_bin, 'wb') as f:
            f.write(img_a)
        print('  wrote raw image -> %s' % a.dump_bin)
    if a.out:
        h = parse_mz(data)
        exe = build_exe(img_a, info, relocs, h['minalloc'], h['maxalloc'])
        with open(a.out, 'wb') as f:
            f.write(exe)
        print('  wrote unpacked MZ -> %s (%d bytes)' % (a.out, len(exe)))
    return 0


if __name__ == '__main__':
    sys.exit(main())
