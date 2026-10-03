#!/usr/bin/env python3
"""Validate the oracle's 80286 behaviour against SingleStepTests/80286 real-silicon vectors.

`sstest.py` holds the oracle to 8088 silicon, but the game is built for a 286 - its inner loops are
full of `enter`/`leave`, `imul reg,r/m,imm`, `shl/sar reg,imm8`, `pusha`, `push imm` and shift counts
masked to five bits, none of which an 8088 has or does the same way. Those were covered by
`test186.c`, written by whoever wrote the emulator, which is the one kind of check that cannot catch
a shared misunderstanding. These vectors were captured from a Harris N80C286-12, in real mode, one
instruction at a time, ~1,000-5,000 cases per opcode (1.5 million in all).

    py tools/x86oracle/sst286.py --dir DIR              # every file in DIR (v1_real_mode/*.MOO.gz)
    py tools/x86oracle/sst286.py --dir DIR 69 6B C0.4   # just those
    py tools/x86oracle/sst286.py --dir DIR --used       # only opcodes our three programs execute
    py tools/x86oracle/sst286.py --dir DIR --verbose --limit=500

Fetch: https://github.com/SingleStepTests/80286/tree/main/v1_real_mode (326 MB; keep it on a local
disk, not a synced folder). MOO format: https://github.com/dbalsom/moo.

Things that are deliberately handled, and why they are not hiding failures:

  * Each vector is `instruction; HLT`, and the recorded final IP is AFTER the HLT, so the expected IP
    is the recorded one minus 1. (The suite's own FAQ: "why is IP always +1 of expected".)
  * The tests assume a 24-bit address bus with A20 on, so code at FFFF:xxxx lives above 1 MB. The
    oracle is an 8086-style 1 MB machine that wraps, so every address is masked to 20 bits - and a
    test whose masked addresses COLLIDE (two cells that differ only above bit 19, holding different
    values) is skipped and COUNTED, never silently dropped.
  * Port I/O (INS/OUTS, IN/OUT) reads a random value off the bus; there is nothing to validate.
  * Flags the manual calls undefined are not compared, per instruction (undefined_mask below), as in
    sstest.py; where the 286 differs from the 8086 (shift counts masked to 5 bits, imul imm) the
    mask says so.
  * REP string operations are driven to retirement, as the vectors record whole instructions.
"""
from __future__ import annotations

import ctypes
import gzip
import os
import re
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..'))
DLL = os.environ.get('F117R_CPU_DLL') or os.path.join(ROOT, 'build', 'f117cpu_api.dll')

REG_IDX = {'ax': 0, 'cx': 1, 'dx': 2, 'bx': 3, 'sp': 4, 'bp': 5, 'si': 6, 'di': 7,
           'es': 8, 'cs': 9, 'ss': 10, 'ds': 11, 'ip': 12, 'flags': 13}
MOO_REGS = ['ax', 'bx', 'cx', 'dx', 'cs', 'ss', 'ds', 'es', 'sp', 'bp', 'si', 'di', 'ip', 'flags']
CF, PF, AF, ZF, SF, OF = 0x001, 0x004, 0x010, 0x040, 0x080, 0x800
ALL_ARITH = CF | PF | AF | ZF | SF | OF
COMPARED = 0x0FD5            # CF PF AF ZF SF TF IF DF OF - the bits that exist in real-mode FLAGS

# no data to compare: the value comes off the bus
IO_OPS = {0x6C, 0x6D, 0x6E, 0x6F, 0xE4, 0xE5, 0xE6, 0xE7, 0xEC, 0xED, 0xEE, 0xEF}
STRING_OPS = set(range(0xA4, 0xA8)) | set(range(0xAA, 0xB0)) | {0x6C, 0x6D, 0x6E, 0x6F}
PREFIXES = {0x26, 0x2E, 0x36, 0x3E, 0xF0, 0xF1, 0xF2, 0xF3}
GROUPS = {0x80, 0x81, 0x82, 0x83, 0x8F, 0xC0, 0xC1, 0xC6, 0xC7, 0xD0, 0xD1, 0xD2, 0xD3,
          0xF6, 0xF7, 0xFE, 0xFF}


def undefined_mask(op, sub, regs, byts):
    """Flags left undefined for this instruction on a 286."""
    if op in (0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25,
              0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x84, 0x85, 0xA8, 0xA9):
        return AF
    if op in (0x27, 0x2F):
        return OF
    if op in (0x37, 0x3F):
        return OF | SF | ZF | PF
    if op in (0xD4, 0xD5):
        return OF | AF | CF
    if op in (0x80, 0x81, 0x82, 0x83):
        return AF if sub in (1, 4, 6) else 0
    if op in (0x69, 0x6B):                      # imul r16, r/m16, imm: SF ZF AF PF undefined
        return SF | ZF | AF | PF
    if op in (0xC0, 0xC1, 0xD0, 0xD1, 0xD2, 0xD3):
        # the count: CL for D2/D3, the immediate byte for C0/C1 (the last one), 1 for D0/D1.
        if op in (0xD2, 0xD3):
            count = regs.get('cx', 0) & 0xFF
        elif op in (0xC0, 0xC1):
            count = byts[-2] if byts and byts[-1] == 0xF4 else byts[-1]
        else:
            count = 1
        count &= 0x1F                            # the 186/286 mask the count to five bits
        m = AF
        if count != 1:
            m |= OF                              # OF is defined only for a count of 1
        if sub in (0, 1, 2, 3):                  # rotates: SF ZF PF are not touched at all
            m |= 0
        return m
    if op in (0xF6, 0xF7):
        if sub in (0, 1):
            return AF
        if sub in (2, 3):
            return 0
        if sub in (4, 5):
            return SF | ZF | AF | PF
        return ALL_ARITH
    return 0


class Oracle:
    def __init__(self):
        if not os.path.exists(DLL):
            sys.exit('x86oracle.dll not built - run tools/x86oracle/build.cmd first')
        self.lib = ctypes.CDLL(DLL)
        L = self.lib
        L.orc_new.restype = ctypes.c_void_p
        L.orc_set_model.argtypes = [ctypes.c_void_p, ctypes.c_int]
        for fn, args in (('orc_free', [ctypes.c_void_p]), ('orc_reset', [ctypes.c_void_p]),
                         ('orc_set_reg', [ctypes.c_void_p, ctypes.c_int, ctypes.c_uint32]),
                         ('orc_write8', [ctypes.c_void_p, ctypes.c_uint32, ctypes.c_uint8])):
            getattr(L, fn).argtypes = args
        L.orc_get_reg.argtypes = [ctypes.c_void_p, ctypes.c_int]
        L.orc_get_reg.restype = ctypes.c_uint32
        L.orc_read8.argtypes = [ctypes.c_void_p, ctypes.c_uint32]
        L.orc_read8.restype = ctypes.c_uint8
        L.orc_step.argtypes = [ctypes.c_void_p]
        L.orc_step.restype = ctypes.c_int
        self.h = L.orc_new()
        L.orc_set_model(self.h, 286)

    def reset(self):
        self.lib.orc_reset(self.h)
        self.lib.orc_set_model(self.h, 286)         # a reset must not drop us back to an 8086

    def get(self, n):
        return self.lib.orc_get_reg(self.h, REG_IDX[n])

    def set(self, n, v):
        self.lib.orc_set_reg(self.h, REG_IDX[n], v & 0xFFFF)

    def r8(self, a):
        return self.lib.orc_read8(self.h, a & 0xFFFFF)

    def w8(self, a, v):
        self.lib.orc_write8(self.h, a & 0xFFFFF, v & 0xFF)

    def step(self):
        return self.lib.orc_step(self.h)


def parse_state(mv, off, length):
    end = off + length
    regs, ram = {}, []
    while off < end:
        tag = bytes(mv[off:off + 4]); off += 4
        sub = struct.unpack_from('<I', mv, off)[0]; off += 4
        if tag == b'REGS':
            mask = struct.unpack_from('<H', mv, off)[0]
            p = off + 2
            for i, name in enumerate(MOO_REGS):
                if mask & (1 << i):
                    regs[name] = struct.unpack_from('<H', mv, p)[0]; p += 2
        elif tag == b'RAM ':
            n = struct.unpack_from('<I', mv, off)[0]
            p = off + 4
            for _ in range(n):
                a, b = struct.unpack_from('<IB', mv, p); p += 5
                ram.append((a, b))
        off += sub
    return regs, ram


def read_tests(path, limit=None):
    """Yield (name, bytes, init_regs, init_ram, final_regs, final_ram, exception) per vector."""
    data = gzip.open(path, 'rb').read()
    if data[:4] != b'MOO ':
        raise ValueError('not a MOO file: ' + path)
    mv = memoryview(data)
    off = 4
    hlen = struct.unpack_from('<I', mv, off)[0]; off += 4 + hlen
    n = 0
    while off < len(data):
        tag = bytes(mv[off:off + 4]); off += 4
        length = struct.unpack_from('<I', mv, off)[0]; off += 4
        if tag == b'TEST':
            p, end = off + 4, off + length
            name, byts, init, final, exc = '?', [], ({}, []), ({}, []), None
            while p < end:
                st = bytes(mv[p:p + 4]); p += 4
                sl = struct.unpack_from('<I', mv, p)[0]; p += 4
                if st == b'NAME':
                    nl = struct.unpack_from('<I', mv, p)[0]
                    name = bytes(mv[p + 4:p + 4 + nl]).decode('ascii', 'replace')
                elif st == b'BYTS':
                    c = struct.unpack_from('<I', mv, p)[0]
                    byts = list(mv[p + 4:p + 4 + c])
                elif st == b'INIT':
                    init = parse_state(mv, p, sl)
                elif st == b'FINA':
                    final = parse_state(mv, p, sl)
                elif st == b'EXCP':
                    exc = struct.unpack_from('<B', mv, p)[0]
                p += sl
            yield name, byts, init[0], init[1], final[0], final[1], exc
            n += 1
            if limit and n >= limit:
                return
        off += length


def opcode_of(byts):
    """(opcode byte, ModRM.reg or None) of the instruction, ignoring prefixes and the trailing HLT."""
    i = 0
    while i < len(byts) and byts[i] in PREFIXES:
        i += 1
    if i >= len(byts):
        return None, None
    op = byts[i]
    sub = ((byts[i + 1] >> 3) & 7) if op in GROUPS and i + 1 < len(byts) else None
    return op, sub


def is_rep_string(byts):
    rep = False
    for b in byts:
        if b in (0xF2, 0xF3):
            rep = True
        elif b in PREFIXES:
            continue
        else:
            return rep and b in STRING_OPS
    return False


def run_file(orc, path, limit=None):
    n = skipped_wrap = 0
    fails = []
    for name, byts, ireg, iram, freg, fram, exc in read_tests(path, limit):
        op, sub = opcode_of(byts)
        if op in IO_OPS:
            continue
        n += 1
        # wrap every address to 20 bits; skip a vector whose cells collide there
        cells = {}
        collide = False
        for a, v in iram:
            m = a & 0xFFFFF
            if m in cells and cells[m] != v:
                collide = True
                break
            cells[m] = v
        if collide:
            skipped_wrap += 1
            n -= 1
            continue
        orc.reset()
        for k, v in ireg.items():
            orc.set(k, v)
        for m, v in cells.items():
            orc.w8(m, v)
        start = (orc.get('cs'), orc.get('ip'))
        rc = orc.step()
        if is_rep_string(byts):
            guard = 0
            while (orc.get('cs'), orc.get('ip')) == start and rc == 0 and guard < 70000:
                rc = orc.step()
                guard += 1

        want = dict(ireg)
        want.update(freg)
        want['ip'] = (want['ip'] - 1) & 0xFFFF       # the injected HLT
        bad = []
        mask = COMPARED & ~undefined_mask(op, sub, ireg, byts)
        for rn in ('ax', 'bx', 'cx', 'dx', 'cs', 'ss', 'ds', 'es', 'sp', 'bp', 'si', 'di', 'ip', 'flags'):
            if rn not in want:
                continue
            got, exp = orc.get(rn), want[rn] & 0xFFFF
            if rn == 'flags':
                if (got & mask) != (exp & mask):
                    bad.append('flags got=%04X exp=%04X diff=%04X' % (got, exp, (got ^ exp) & mask))
            elif got != exp:
                bad.append('%s got=%04X exp=%04X' % (rn, got, exp))
        expmem = dict(cells)
        for a, v in fram:
            expmem[a & 0xFFFFF] = v
        # an exception pushes FLAGS whose undefined bits are microcode residue - not compared
        skip = set()
        if exc is not None:
            sp0, ss = ireg.get('sp', 0), ireg.get('ss', 0)
            skip = {(((ss << 4) + ((sp0 - 2 - 2 * k) & 0xFFFF)) & 0xFFFFF) + j
                    for k in range(1) for j in range(2)}
        for m, v in sorted(expmem.items()):
            if m in skip:
                continue
            if orc.r8(m) != v:
                bad.append('mem[%05X] got=%02X exp=%02X' % (m, orc.r8(m), v))
        if bad or rc != 0:
            fails.append((name, bytes(byts).hex(' '), bad, rc, exc,
                          bucket(name, byts, ireg, fram, exc)))
    return n, skipped_wrap, fails


# Every failure is put in a NAMED bucket or left unexplained, and only the unexplained ones fail the
# run. Each bucket is a reason the disagreement cannot matter to a program that runs on the real
# machine, stated here so it can be argued with - a silent skip list would be the same mistake as the
# ones this suite exists to catch.
BUCKETS = {
    'gp13': 'the silicon raised #GP at the 64 KB segment boundary (a word access at offset FFFF); '
            'this 8086-style core wraps. A program that runs on a 286 or any 386+ cannot rely on it.',
    'illegal': 'an encoding the 286 faults on (register-operand lea/les/lds/bound/far call, mov cs, '
               'mov fs/gs, the unused group members): the vectors name them "(bad)". A working '
               'program never executes one.',
    'hlt-ip': 'HLT itself: the vectors record IP after the injected second HLT. The game never halts.',
    'aam0-flags': 'AAM with a zero base is a divide fault whose flag residue the manual leaves '
                  'undefined.',
    'idiv8-corner': 'byte IDIV of a very negative dividend: the silicon returns AL=80 instead of '
                    'faulting (2 of 756 overflow cases). The game executes unsigned byte DIV, which '
                    'passes every vector, and no byte IDIV.',
    'enter-stackwrap': 'ENTER with a nesting level above 0 on a stack that wraps the segment '
                       '(SP=BP=FFFF). The game only ever uses level 0.',
    'rep-overwrites-code': 'a REP string instruction whose stores overwrite the instruction itself: '
                           'silicon runs from its prefetch queue, this core re-fetches each '
                           'iteration. Self-modifying string stores are not something the game does.',
}


def bucket(name, byts, ireg, fram, exc):
    """The named reason a failing vector cannot matter, or None when it is unexplained."""
    if exc == 13:
        return 'gp13'
    if '(bad)' in name:
        return 'illegal'
    i = 0
    while i < len(byts) and byts[i] in PREFIXES:
        i += 1
    op = byts[i] if i < len(byts) else None
    modrm = byts[i + 1] if i + 1 < len(byts) else 0
    if op == 0x8E and ((modrm >> 3) & 7) not in (0, 2, 3):
        return 'illegal'                              # mov cs / fs / gs
    if op == 0x8C and ((modrm >> 3) & 7) > 3:
        return 'illegal'
    if op == 0xF4:
        return 'hlt-ip'
    if op == 0xD4 and modrm == 0:
        return 'aam0-flags'
    if op == 0xF6 and ((modrm >> 3) & 7) == 7:
        return 'idiv8-corner'
    if op == 0xC8 and len(byts) > i + 3 and (byts[i + 3] & 0x1F) > 0 \
       and (ireg.get('sp', 0) >= 0xFFF0 or ireg.get('bp', 0) >= 0xFFF0):
        return 'enter-stackwrap'
    if is_rep_string(byts):
        code = (((ireg.get('cs', 0) << 4) + ireg.get('ip', 0)) & 0xFFFFF)
        span = {(code + k) & 0xFFFFF for k in range(len(byts))}
        if any((a & 0xFFFFF) in span for a, _ in fram):
            return 'rep-overwrites-code'
    return None


FILE_RE = re.compile(r'^([0-9A-F]{2,4})(?:\.([0-7]))?\.MOO\.gz$')

# 326 MB, so NOT under the repository (which sits in a synced folder): build.cmd keeps its output
# in ~/f117-local for the same reason.
DEFAULT_DIR = os.environ.get('F117R_SST286_DIR') or os.path.join(os.path.expanduser('~'), 'f117-local', 'sst286', 'v1_real_mode')
RAW = 'https://raw.githubusercontent.com/SingleStepTests/80286/main/v1_real_mode/'
API = 'https://api.github.com/repos/SingleStepTests/80286/contents/v1_real_mode'


def fetch(d):
    """Download every vector file that is not already there."""
    import json
    import urllib.request
    os.makedirs(d, exist_ok=True)
    with urllib.request.urlopen(API, timeout=60) as r:
        names = [(e['name'], e['size']) for e in json.load(r) if FILE_RE.match(e['name'])]
    got = 0
    for name, size in names:
        dest = os.path.join(d, name)
        if os.path.exists(dest) and os.path.getsize(dest) == size:
            continue
        urllib.request.urlretrieve(RAW + name, dest)
        got += 1
    print('%d file(s) in %s (%d fetched now)' % (len(names), d, got))
    return 0


def game_ops():
    """The (opcode, ModRM.reg) pairs our three programs actually execute, from the census."""
    sys.path.insert(0, os.path.join(ROOT, 'tools', 'f117'))
    import capstone
    import branchcover as bc
    used = set()
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_16)
    for name in ('VGAME', 'START', 'END', 'MPS_LOGO'):
        try:
            insns, _s, _z = bc.census(name)
        except Exception:
            continue
        path = None
        for cand in (os.path.join(ROOT, 'local', 'unpacked', '%s.bin' % name),
                     os.path.join(ROOT, 'local', '%s-unpacked.bin' % name.lower())):
            if os.path.exists(cand):
                path = cand
                break
        if not path:
            continue
        data = open(path, 'rb').read()
        for a in insns:
            i = next(md.disasm(data[a:a + 16], a), None)
            if i:
                op, sub = opcode_of(bytes(i.bytes))
                if op is not None:
                    used.add((op, sub))
    return used


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    flags = [a for a in sys.argv[1:] if a.startswith('--')]
    verbose = '--verbose' in flags
    limit = next((int(f.split('=')[1]) for f in flags if f.startswith('--limit=')), None)
    d = os.environ.get('F117_SST286') or DEFAULT_DIR
    if '--dir' in sys.argv:
        d = sys.argv[sys.argv.index('--dir') + 1]
        args = [a for a in args if a != d]
    if '--fetch' in flags:
        return fetch(d)
    if not os.path.isdir(d) or not any(FILE_RE.match(f) for f in os.listdir(d)):
        print('no 286 vectors in %s - run: py tools/x86oracle/sst286.py --fetch' % d)
        return 77                                  # "not run", as verify.py reads it
    want = None
    if args:
        want = set(a.upper() for a in args)
    used = game_ops() if '--used' in flags else None

    files = []
    for f in sorted(os.listdir(d)):
        m = FILE_RE.match(f)
        if not m:
            continue
        label = m.group(1) + ('.' + m.group(2) if m.group(2) else '')
        if want and label not in want and m.group(1) not in want:
            continue
        if len(m.group(1)) == 4:                       # 0Fxx: protected-mode / system, never executed
            continue
        op = int(m.group(1), 16)
        sub = int(m.group(2)) if m.group(2) else None
        if used is not None and (op, sub) not in used and (op, None) not in used:
            continue
        files.append((f, label, op, sub))
    if not files:
        sys.exit('no vector files selected')

    orc = Oracle()
    total = wrap = 0
    counts = {}
    unexplained = []
    for f, label, op, sub in files:
        n, w, fails = run_file(orc, os.path.join(d, f), limit)
        total += n
        wrap += w
        bad_here = [x for x in fails if x[5] is None]
        for x in fails:
            if x[5] is not None:
                counts[x[5]] = counts.get(x[5], 0) + 1
        if bad_here:
            unexplained.append((len(bad_here), label, bad_here))
            print('%-7s %-24s UNEXPLAINED %d/%d' % (label, bad_here[0][0][:24], len(bad_here), n))
            if verbose:
                for nm, bts, bad, rc, exc, _b in bad_here[:3]:
                    print('        %-26s [%s] rc=%d exc=%s' % (nm, bts, rc, exc))
                    for b in bad[:5]:
                        print('           ' + b)
        elif verbose:
            print('%-7s ok (%d%s)' % (label, n, '' if not fails else ', %d explained' % len(fails)))
    nun = sum(n for n, _, _ in unexplained)
    print()
    print('=' * 64)
    print('TOTAL %d vectors across %d files: %d pass, %d explained, %d UNEXPLAINED'
          % (total, len(files), total - nun - sum(counts.values()), sum(counts.values()), nun))
    for k in sorted(counts, key=lambda k: -counts[k]):
        print('   %-20s %6d  %s' % (k, counts[k], BUCKETS[k][:96]))
    print('   %-20s %6d  skipped: two cells that differ only above bit 19 hold different values'
          % ('(address collision)', wrap))
    if unexplained:
        unexplained.sort(reverse=True)
        print('unexplained, by opcode: ' + ', '.join('%s(%d)' % (lbl, n) for n, lbl, _ in unexplained[:16]))
    print('286 silicon vectors: %d of %d agree, %d explained by a named bucket, %d unexplained'
          % (total - nun - sum(counts.values()), total, sum(counts.values()), nun))
    return 1 if nun else 0


if __name__ == '__main__':
    sys.exit(main())
