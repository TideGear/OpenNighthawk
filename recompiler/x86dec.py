"""x86dec.py - decode 8086/80186/80286 real-mode instructions exactly as the
interpreter (src/cpu/cpu.c, cpu_step) decodes them.

The recompiler's translation of an instruction is only as good as its
agreement with cpu_step about where the instruction ends and what its
fields are, so this mirrors cpu_step's decode case for case - including the
corners real silicon faults on (register-form LEA/LES/LDS/BOUND/far calls
read through segment 0, offset 0 as cpu_step does) - and is checked against
the silicon vectors by tests/sst_recomp.py.

The CPU model is the 80286 (the machine's), so 0x60-0x6F are the 80186
instructions, C0/C1 shift by an immediate, C8/C9 are ENTER/LEAVE, and 0x0F
and 0x63-0x67 are invalid.
"""
from __future__ import annotations

from dataclasses import dataclass, field

S_ES, S_CS, S_SS, S_DS = 0, 1, 2, 3
SEG_NAMES = ["S_ES", "S_CS", "S_SS", "S_DS"]

PREFIX_SEG = {0x26: S_ES, 0x2E: S_CS, 0x36: S_SS, 0x3E: S_DS}

# Control-flow kinds.
K_NORMAL = "normal"      # falls through
K_JCC = "jcc"            # conditional rel8 (Jcc, LOOPx, JCXZ): target or next
K_JMP = "jmp"            # unconditional near relative
K_JMPFAR = "jmpfar"      # EA ptr16:16
K_CALL = "call"          # near relative call
K_CALLFAR = "callfar"    # 9A ptr16:16
K_RET = "ret"            # RET, RETF, IRET: no static successor
K_JMPIND = "jmpind"      # FF /4, FF /5
K_CALLIND = "callind"    # FF /2, FF /3
K_INT = "int"            # CC, CD, CE: may not return to next
K_HLT = "hlt"
K_INVALID = "invalid"    # cpu_step faults


@dataclass
class ModRM:
    mod: int
    reg: int
    rm: int
    disp: int = 0            # as encoded (unsigned 16-bit after sign extension)
    disp_off: int = -1       # byte offset of the displacement in the instruction
    disp_size: int = 0

    @property
    def is_reg(self):
        return self.mod == 3


@dataclass
class Insn:
    ip: int                  # offset of the first byte (prefixes included) within CS
    length: int
    raw: bytes
    op: int = 0
    seg_ovr: int = -1        # -1 or S_*
    rep: int = 0             # 0, 0xF2, 0xF3
    modrm: ModRM | None = None
    imm: int = 0
    imm_off: int = -1        # byte offset of the immediate
    imm_size: int = 0
    imm2: int = 0            # ENTER level, far pointer segment
    imm2_off: int = -1
    imm2_size: int = 0
    kind: str = K_NORMAL
    target: int = -1         # near branch target (IP) when static
    far_seg: int = -1
    far_off: int = -1

    @property
    def next_ip(self):
        return (self.ip + self.length) & 0xFFFF

    @property
    def w16(self):
        return self.op & 1


class DecodeError(Exception):
    pass


def _modrm(code, pos, end):
    if pos >= end:
        raise DecodeError("truncated modrm")
    b = code[pos]
    m = ModRM(mod=(b >> 6) & 3, reg=(b >> 3) & 7, rm=b & 7)
    pos += 1
    if m.mod == 3:
        return m, pos
    if m.mod == 0 and m.rm == 6:
        if pos + 2 > end:
            raise DecodeError("truncated disp16")
        m.disp = code[pos] | (code[pos + 1] << 8)
        m.disp_off, m.disp_size = pos, 2
        pos += 2
    elif m.mod == 1:
        if pos >= end:
            raise DecodeError("truncated disp8")
        d = code[pos]
        m.disp = (d - 256 if d >= 0x80 else d) & 0xFFFF
        m.disp_off, m.disp_size = pos, 1
        pos += 1
    elif m.mod == 2:
        if pos + 2 > end:
            raise DecodeError("truncated disp16")
        m.disp = code[pos] | (code[pos + 1] << 8)
        m.disp_off, m.disp_size = pos, 2
        pos += 2
    return m, pos


def decode(code, start, ip, limit=None):
    """Decode one instruction from `code` (bytes) at index `start`, which the
    CPU sees at offset `ip` of its code segment. Returns an Insn whose
    imm_off/disp_off are relative to the instruction's first byte."""
    end = len(code) if limit is None else min(len(code), limit)
    pos = start
    seg_ovr, rep = -1, 0
    while True:
        if pos >= end:
            raise DecodeError("truncated")
        op = code[pos]
        pos += 1
        if op in PREFIX_SEG:
            seg_ovr = PREFIX_SEG[op]
        elif op in (0xF0, 0xF1):
            pass
        elif op in (0xF2, 0xF3):
            rep = op
        else:
            break
        if pos - start > 15:
            raise DecodeError("prefix run")

    ins = Insn(ip=ip, length=0, raw=b"", op=op, seg_ovr=seg_ovr, rep=rep)

    def imm8():
        nonlocal pos
        if pos >= end:
            raise DecodeError("truncated imm8")
        v = code[pos]
        ins.imm, ins.imm_off, ins.imm_size = v, pos - start, 1
        pos += 1

    def imm16():
        nonlocal pos
        if pos + 2 > end:
            raise DecodeError("truncated imm16")
        v = code[pos] | (code[pos + 1] << 8)
        ins.imm, ins.imm_off, ins.imm_size = v, pos - start, 2
        pos += 2

    def modrm():
        nonlocal pos
        m, pos = _modrm(code, pos, end)
        if m.disp_off >= 0:
            m.disp_off -= start
        ins.modrm = m

    def rel8():
        imm8()
        r = ins.imm - 256 if ins.imm >= 0x80 else ins.imm
        return r

    def rel16():
        imm16()
        r = ins.imm - 65536 if ins.imm >= 0x8000 else ins.imm
        return r

    def finish():
        ins.length = pos - start
        ins.raw = bytes(code[start:pos])
        return ins

    # --- by opcode --------------------------------------------------------
    if op < 0x40:
        low = op & 7
        if op == 0x0F or op in (0x26, 0x2E, 0x36, 0x3E):
            ins.kind = K_INVALID          # 0x0F faults on the 286
            return finish()
        if low <= 3:
            modrm()
        elif low == 4:
            imm8()
        elif low == 5:
            imm16()
        # 06/07/0E/16/17/1E/1F push/pop seg; 27/2F/37/3F BCD: no operands
        return finish()
    if op < 0x60:
        return finish()                    # INC/DEC/PUSH/POP reg
    if op < 0x70:
        if op in (0x60, 0x61):
            return finish()
        if op == 0x62:
            modrm()
            # BOUND may raise INT 5
            return finish()
        if op == 0x68:
            imm16()
            return finish()
        if op == 0x6A:
            imm8()
            return finish()
        if op == 0x69:
            modrm()
            imm16()
            return finish()
        if op == 0x6B:
            modrm()
            imm8()
            return finish()
        if op in (0x6C, 0x6D, 0x6E, 0x6F):
            return finish()
        ins.kind = K_INVALID              # 0x63..0x67
        return finish()
    if op < 0x80:
        r = rel8()
        ins.kind = K_JCC
        ins.target = (ip + (pos - start) + r) & 0xFFFF
        return finish()
    if op < 0x84:
        modrm()
        if op == 0x81:
            imm16()
        else:
            imm8()
        return finish()
    if op < 0x90:
        modrm()
        return finish()
    if op < 0x9A:
        return finish()
    if op == 0x9A:
        imm16()
        o = ins.imm
        ins.far_off = o
        if pos + 2 > end:
            raise DecodeError("truncated far ptr")
        ins.imm2 = code[pos] | (code[pos + 1] << 8)
        ins.imm2_off, ins.imm2_size = pos - start, 2
        ins.far_seg = ins.imm2
        pos += 2
        ins.kind = K_CALLFAR
        return finish()
    if op < 0xA0:
        return finish()
    if op < 0xA4:
        imm16()                           # moffs
        return finish()
    if op < 0xA8:
        return finish()
    if op == 0xA8:
        imm8()
        return finish()
    if op == 0xA9:
        imm16()
        return finish()
    if op < 0xB0:
        return finish()
    if op < 0xB8:
        imm8()
        return finish()
    if op < 0xC0:
        imm16()
        return finish()
    if op in (0xC0, 0xC1):
        modrm()
        imm8()
        return finish()
    if op == 0xC2:
        imm16()
        ins.kind = K_RET
        return finish()
    if op == 0xC3:
        ins.kind = K_RET
        return finish()
    if op in (0xC4, 0xC5):
        modrm()
        return finish()
    if op == 0xC6:
        modrm()
        imm8()
        return finish()
    if op == 0xC7:
        modrm()
        imm16()
        return finish()
    if op == 0xC8:
        imm16()
        if pos >= end:
            raise DecodeError("truncated enter")
        ins.imm2 = code[pos]
        ins.imm2_off, ins.imm2_size = pos - start, 1
        pos += 1
        return finish()
    if op == 0xC9:
        return finish()
    if op == 0xCA:
        imm16()
        ins.kind = K_RET
        return finish()
    if op == 0xCB:
        ins.kind = K_RET
        return finish()
    if op == 0xCC:
        ins.kind = K_INT
        return finish()
    if op == 0xCD:
        imm8()
        ins.kind = K_INT
        return finish()
    if op == 0xCE:
        ins.kind = K_INT
        return finish()
    if op == 0xCF:
        ins.kind = K_RET
        return finish()
    if op < 0xD4:
        modrm()
        return finish()
    if op in (0xD4, 0xD5):
        imm8()
        return finish()
    if op in (0xD6, 0xD7):
        return finish()
    if op < 0xE0:
        modrm()                           # ESC
        return finish()
    if op < 0xE4:
        r = rel8()
        ins.kind = K_JCC
        ins.target = (ip + (pos - start) + r) & 0xFFFF
        return finish()
    if op < 0xE8:
        imm8()                            # IN/OUT imm8
        return finish()
    if op == 0xE8:
        r = rel16()
        ins.kind = K_CALL
        ins.target = (ip + (pos - start) + r) & 0xFFFF
        return finish()
    if op == 0xE9:
        r = rel16()
        ins.kind = K_JMP
        ins.target = (ip + (pos - start) + r) & 0xFFFF
        return finish()
    if op == 0xEA:
        imm16()
        ins.far_off = ins.imm
        if pos + 2 > end:
            raise DecodeError("truncated far ptr")
        ins.imm2 = code[pos] | (code[pos + 1] << 8)
        ins.imm2_off, ins.imm2_size = pos - start, 2
        ins.far_seg = ins.imm2
        pos += 2
        ins.kind = K_JMPFAR
        return finish()
    if op == 0xEB:
        r = rel8()
        ins.kind = K_JMP
        ins.target = (ip + (pos - start) + r) & 0xFFFF
        return finish()
    if op < 0xF0:
        return finish()                   # IN/OUT DX
    if op == 0xF4:
        ins.kind = K_HLT
        return finish()
    if op == 0xF5:
        return finish()
    if op in (0xF6, 0xF7):
        modrm()
        if ins.modrm.reg in (0, 1):
            if op == 0xF6:
                imm8()
            else:
                imm16()
        return finish()
    if op < 0xFE:
        return finish()                   # F8..FD flag ops
    if op == 0xFE:
        modrm()
        return finish()
    # 0xFF
    modrm()
    reg = ins.modrm.reg
    if reg in (2, 3):
        ins.kind = K_CALLIND
    elif reg in (4, 5):
        ins.kind = K_JMPIND
    return finish()


def successors(ins):
    """Static control-flow successors within the code segment (IPs), and
    whether the instruction can continue to the next one."""
    k = ins.kind
    if k == K_NORMAL:
        return [ins.next_ip]
    if k == K_JCC:
        return [ins.target, ins.next_ip]
    if k == K_JMP:
        return [ins.target]
    if k in (K_CALL, K_CALLFAR, K_CALLIND, K_INT, K_HLT):
        return [ins.next_ip]
    return []
