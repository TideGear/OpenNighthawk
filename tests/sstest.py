#!/usr/bin/env python3
"""Validate the 8086 oracle against the SingleStepTests/8088 vectors.

Those vectors were captured from real 8088 silicon: 10,000 randomised cases
per opcode (per group member, for the ModR/M-extended opcodes), each
recording the complete CPU and memory state before and after one
instruction. Passing them is the strongest available evidence that the
oracle's observations of the game can be trusted.

Usage:
  sstest.py --fetch [opcodes...]     download vector files into local/sstests
  sstest.py [opcodes...]             run them (default: everything cached)
  sstest.py --verbose                show the first few failures per file
  sstest.py --limit=N                cap tests per file

Opcodes may be given as hex ("f7"), ranges ("80-83"), or "all".

Undefined flags: where Intel documents a flag as undefined for an
instruction, the vectors still record whatever the silicon produced. Holding
a reimplementation to that would be meaningless, so those bits are masked
out of the comparison per group member (see undefined_mask). Everything else
is compared exactly, including memory.
"""
from __future__ import annotations

import ctypes
import gzip
import json
import os
import re
import sys
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..'))
CACHE = os.environ.get('F117R_SST_CACHE') or os.path.join(ROOT, 'local', 'sstests')
DLL = os.environ.get('F117R_CPU_DLL') or os.path.join(ROOT, 'build', 'f117cpu_api.dll')
BASE_URL = 'https://raw.githubusercontent.com/SingleStepTests/8088/main/v2/'
API_URL = ('https://api.github.com/repos/SingleStepTests/8088/'
           'contents/v2?per_page=400')

REG_IDX = {'ax': 0, 'cx': 1, 'dx': 2, 'bx': 3, 'sp': 4, 'bp': 5, 'si': 6,
           'di': 7, 'es': 8, 'cs': 9, 'ss': 10, 'ds': 11, 'ip': 12,
           'flags': 13}
REG_ORDER = ['ax', 'cx', 'dx', 'bx', 'sp', 'bp', 'si', 'di',
             'es', 'cs', 'ss', 'ds', 'ip', 'flags']

CF, PF, AF, ZF, SF, OF = 0x001, 0x004, 0x010, 0x040, 0x080, 0x800
ALL_ARITH = CF | PF | AF | ZF | SF | OF
COMPARED = 0x0FD5          # the bits that actually exist in FLAGS


def undefined_mask(op, sub, initial_regs):
    """Flags Intel leaves undefined for this instruction, so not compared.

    `sub` is the ModR/M reg field for group opcodes, else None. Some cases
    depend on the operand (shift counts), hence initial_regs.
    """
    # Logic ops: AF undefined.
    if op in (0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D,
              0x20, 0x21, 0x22, 0x23, 0x24, 0x25,
              0x30, 0x31, 0x32, 0x33, 0x34, 0x35,
              0x84, 0x85, 0xA8, 0xA9):
        return AF
    if op in (0x27, 0x2F):                      # DAA / DAS
        return OF
    if op in (0x37, 0x3F):                      # AAA / AAS
        return OF | SF | ZF | PF
    if op in (0xD4, 0xD5):                      # AAM / AAD
        return OF | AF | CF

    # Group 1 (0x80..0x83): AF undefined only for the logic members.
    if op in (0x80, 0x81, 0x82, 0x83):
        return AF if sub in (1, 4, 6) else 0

    # Groups 2 (0xD0..0xD3): shifts and rotates.
    if op in (0xD0, 0xD1, 0xD2, 0xD3):
        by_cl = op in (0xD2, 0xD3)
        count = (initial_regs.get('cx', 0) & 0xFF) if by_cl else 1
        m = AF                                  # AF always undefined here
        if count != 1:
            m |= OF                             # OF defined only for count 1
        if sub in (0, 1, 2, 3):                 # rotates leave SZP alone
            pass
        return m

    # Group 3 (0xF6/0xF7).
    if op in (0xF6, 0xF7):
        if sub in (0, 1):                       # TEST
            return AF
        if sub == 2:                            # NOT touches no flags
            return 0
        if sub == 3:                            # NEG is fully defined
            return 0
        if sub in (4, 5):                       # MUL / IMUL
            return SF | ZF | AF | PF
        return ALL_ARITH                        # DIV / IDIV: all undefined

    return 0


class Oracle:
    def __init__(self):
        if not os.path.exists(DLL):
            sys.exit('x86oracle.dll not built - run build.cmd first')
        self.lib = ctypes.CDLL(DLL)
        L = self.lib
        L.orc_new.restype = ctypes.c_void_p
        for fn, args in (
            ('orc_free', [ctypes.c_void_p]),
            ('orc_reset', [ctypes.c_void_p]),
            ('orc_clear_mem', [ctypes.c_void_p]),
            ('orc_set_reg', [ctypes.c_void_p, ctypes.c_int, ctypes.c_uint32]),
            ('orc_write8', [ctypes.c_void_p, ctypes.c_uint32, ctypes.c_uint8]),
        ):
            getattr(L, fn).argtypes = args
        L.orc_get_reg.argtypes = [ctypes.c_void_p, ctypes.c_int]
        L.orc_get_reg.restype = ctypes.c_uint32
        L.orc_read8.argtypes = [ctypes.c_void_p, ctypes.c_uint32]
        L.orc_read8.restype = ctypes.c_uint8
        L.orc_step.argtypes = [ctypes.c_void_p]
        L.orc_step.restype = ctypes.c_int
        self.h = L.orc_new()

    def __del__(self):
        try:
            self.lib.orc_free(self.h)
        except Exception:
            pass

    reset = lambda self: self.lib.orc_reset(self.h)                # noqa: E731
    get = lambda self, n: self.lib.orc_get_reg(self.h, REG_IDX[n])  # noqa: E731
    r8 = lambda self, a: self.lib.orc_read8(self.h, a & 0xFFFFF)    # noqa: E731

    def set(self, n, v):
        self.lib.orc_set_reg(self.h, REG_IDX[n], v & 0xFFFF)

    def w8(self, a, v):
        self.lib.orc_write8(self.h, a & 0xFFFFF, v & 0xFF)

    def step(self):
        return self.lib.orc_step(self.h)


def parse_opcodes(args):
    if not args or 'all' in args:
        return set(range(0x100))
    out = set()
    for a in args:
        if '-' in a:
            lo, hi = a.split('-')
            out.update(range(int(lo, 16), int(hi, 16) + 1))
        else:
            out.add(int(a, 16))
    return out


def remote_index():
    with urllib.request.urlopen(API_URL, timeout=60) as r:
        listing = json.load(r)
    return sorted(e['name'] for e in listing if e['name'].endswith('.json.gz'))


def fetch(opcodes):
    os.makedirs(CACHE, exist_ok=True)
    names = [n for n in remote_index() if int(n.split('.')[0], 16) in opcodes]
    got = skipped = failed = 0
    for name in names:
        dest = os.path.join(CACHE, name)
        if os.path.exists(dest) and os.path.getsize(dest) > 0:
            skipped += 1
            continue
        try:
            urllib.request.urlretrieve(BASE_URL + name, dest)
            got += 1
            print('  fetched %s (%d KiB)' % (name, os.path.getsize(dest) // 1024))
        except Exception as e:
            if os.path.exists(dest):
                os.remove(dest)
            failed += 1
            print('  skip %s (%s)' % (name, type(e).__name__))
    print('fetched=%d cached=%d unavailable=%d' % (got, skipped, failed))


FILE_RE = re.compile(r'^([0-9A-F]{2})(?:\.([0-7]))?\.json\.gz$')


def cached_files(opcodes):
    if not os.path.isdir(CACHE):
        return []
    out = []
    for f in sorted(os.listdir(CACHE)):
        m = FILE_RE.match(f)
        if m and int(m.group(1), 16) in opcodes:
            out.append((f, int(m.group(1), 16),
                        int(m.group(2)) if m.group(2) is not None else None))
    return out


STRING_OPS = set(range(0xA4, 0xA8)) | set(range(0xAA, 0xB0))


def _is_rep_string(bts):
    """True if these instruction bytes are a REP-prefixed string operation."""
    seen_rep = False
    for b in bts:
        if b in (0xF2, 0xF3):
            seen_rep = True
        elif b in (0x26, 0x2E, 0x36, 0x3E, 0xF0, 0xF1):
            continue
        else:
            return seen_rep and b in STRING_OPS
    return False


def run_file(orc, path, op, sub, limit=None):
    with gzip.open(path, 'rt', encoding='utf-8') as fh:
        tests = json.load(fh)
    fails = []
    n = 0
    for t in tests:
        if limit and n >= limit:
            break
        n += 1
        init = t['initial']
        orc.reset()
        for k, v in init['regs'].items():
            if k in REG_IDX:
                orc.set(k, v)
        touched = set()
        for addr, val in init.get('ram', []):
            orc.w8(addr, val)
            touched.add(addr)
        exp_ram = {a: v for a, v in t['final'].get('ram', [])}
        touched.update(exp_ram)
        start_cs, start_ip = orc.get('cs'), orc.get('ip')

        # The oracle models REP as interruptible: each cpu_step performs one
        # iteration and rewinds CS:IP so an interrupt can be taken between
        # iterations, which is what the silicon does. The vectors, though,
        # capture one whole instruction, so drive it to retirement here.
        rc = orc.step()
        if _is_rep_string(t['bytes']):
            guard = 0
            while (orc.get('cs'), orc.get('ip')) == (start_cs, start_ip):
                rc = orc.step()
                guard += 1
                if rc != 0 or guard > 70000:
                    break

        # A divide error pushes FLAGS whose undefined bits are microcode
        # residue we do not model. Exclude that word from the memory check.
        skip = set()
        if (op in (0xF6, 0xF7) and sub in (6, 7)) or op == 0xD4:
            if ((init['regs']['sp'] - orc.get('sp')) & 0xFFFF) == 6:
                fa = ((orc.get('ss') << 4) + ((orc.get('sp') + 4) & 0xFFFF)) & 0xFFFFF
                skip = {fa, (fa + 1) & 0xFFFFF}

        want = dict(init['regs'])
        want.update(t['final'].get('regs', {}))
        undef = undefined_mask(op, sub, init['regs'])
        mask = COMPARED & ~undef

        bad = []
        for name in REG_ORDER:
            if name not in want:
                continue
            got, exp = orc.get(name), want[name] & 0xFFFF
            if name == 'flags':
                if (got & mask) != (exp & mask):
                    bad.append('flags got=%04X exp=%04X diff=%04X'
                               % (got, exp, (got ^ exp) & mask))
            elif got != exp:
                bad.append('%s got=%04X exp=%04X' % (name, got, exp))
        for addr in sorted(touched - skip):
            if addr in exp_ram and orc.r8(addr) != exp_ram[addr]:
                bad.append('mem[%05X] got=%02X exp=%02X'
                           % (addr, orc.r8(addr), exp_ram[addr]))
        if bad or rc != 0:
            fails.append((t.get('name', '?'), bytes(t['bytes']).hex(' '), bad, rc))
    return n, fails


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    flags = {a for a in sys.argv[1:] if a.startswith('--')}
    verbose = '--verbose' in flags
    limit = next((int(f.split('=')[1]) for f in flags
                  if f.startswith('--limit=')), None)

    if '--fetch' in flags:
        fetch(parse_opcodes(args))
        return 0

    files = cached_files(parse_opcodes(args))
    if not files:
        sys.exit('no test vectors cached; run with --fetch first')

    orc = Oracle()
    total = tfail = 0
    worst = []
    for fname, op, sub in files:
        n, fails = run_file(orc, os.path.join(CACHE, fname), op, sub, limit)
        total += n
        tfail += len(fails)
        label = fname[:-8]
        if fails:
            worst.append((len(fails), label, fails))
            print('%-7s %-22s FAIL %d/%d'
                  % (label, fails[0][0][:22], len(fails), n))
            if verbose:
                for name, bts, bad, rc in fails[:3]:
                    print('        %-24s [%s] rc=%d' % (name, bts, rc))
                    for b in bad[:5]:
                        print('           %s' % b)
        elif verbose:
            print('%-7s ok (%d)' % (label, n))

    print()
    print('=' * 64)
    print('TOTAL %d tests across %d files, %d failures (%.4f%%)'
          % (total, len(files), tfail, 100.0 * tfail / total if total else 0))
    if worst:
        worst.sort(reverse=True)
        print('worst: %s' % ', '.join('%s(%d)' % (lbl, n)
                                      for n, lbl, _ in worst[:14]))
    return 1 if tfail else 0


if __name__ == '__main__':
    sys.exit(main())
