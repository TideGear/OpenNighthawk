"""emit.py - one decoded instruction as C, mirroring cpu_step case for case.

Every translation calls the helpers in src/cpu/x86_sem.h that cpu_step
calls, in the order cpu_step calls them, so the two engines cannot differ in
an instruction's arithmetic or flags; tests/sst_recomp.py holds the output
of this file to the silicon vectors directly.

The shape of each translated instruction is fixed:

    L_ip:  CHECK(ip);            stop here if an event is due (sets op_ip)
           ...body...            the semantics
           IC();                 the instruction retires (icount++)
           ...transfer...        goto a label in this region, or return

which is the interpreter's loop unrolled: look at events before the
instruction, execute it, count it. Operand bytes that the DOS loader
relocates, and far-jump operands the game patches at run time, are read
from memory when the instruction runs - as the CPU fetches them - rather
than baked in; the caller passes their offsets in `live`.
"""
from __future__ import annotations

from x86dec import (Insn, K_INVALID, S_CS, S_DS, S_ES, S_SS, SEG_NAMES)

REG16 = ["R_AX", "R_CX", "R_DX", "R_BX", "R_SP", "R_BP", "R_SI", "R_DI"]


class Ctx:
    """What the emitter needs to know about the instruction's surroundings.

    in_region(ip) - is there a label for ip in this region?
    live          - set of byte offsets (relative to the instruction) whose
                    values must be read from memory at run time.
    """

    def __init__(self, in_region, live=frozenset()):
        self.in_region = in_region
        self.live = live


def h16(v):
    return "0x%04X" % (v & 0xFFFF)


def h8(v):
    return "0x%02X" % (v & 0xFF)


def transfer(ctx, ip):
    """Continue at a near target: a goto when it is in this region."""
    if ctx.in_region(ip):
        return "goto L_%04X;" % ip
    return "EXIT(%s);" % h16(ip)


def word_at(ins, ctx, off, value):
    """A 16-bit operand at byte offset `off` of the instruction: a constant,
    or a run-time code read when the loader patches it."""
    if off in ctx.live or (off + 1) in ctx.live:
        return "CODE16(%s)" % h16(ins.ip + off)
    return h16(value)


def byte_at(ins, ctx, off, value):
    if off in ctx.live:
        return "CODE8(%s)" % h16(ins.ip + off)
    return h8(value)


def imm_expr(ins, ctx):
    if ins.imm_size == 2:
        return word_at(ins, ctx, ins.imm_off, ins.imm)
    return byte_at(ins, ctx, ins.imm_off, ins.imm)


def default_seg(ins, def_seg):
    return ins.seg_ovr if ins.seg_ovr >= 0 else def_seg


class Operand:
    """The ModR/M operand as cpu_step's modrm_t: either a register or a
    memory location whose segment and offset are fixed when the instruction
    is decoded (before any of its effects)."""

    def __init__(self, ins, ctx):
        m = ins.modrm
        self.m = m
        self.ins = ins
        if m.is_reg:
            self.setup = "const uint16_t s_ = 0, o_ = 0; (void)s_; (void)o_;"
            return
        def_seg = S_DS
        parts = []
        rm = m.rm
        if rm == 0: parts = ["c->r[R_BX]", "c->r[R_SI]"]
        elif rm == 1: parts = ["c->r[R_BX]", "c->r[R_DI]"]
        elif rm == 2: parts = ["c->r[R_BP]", "c->r[R_SI]"]; def_seg = S_SS
        elif rm == 3: parts = ["c->r[R_BP]", "c->r[R_DI]"]; def_seg = S_SS
        elif rm == 4: parts = ["c->r[R_SI]"]
        elif rm == 5: parts = ["c->r[R_DI]"]
        elif rm == 6:
            if m.mod == 0:
                parts = []
            else:
                parts = ["c->r[R_BP]"]
                def_seg = S_SS
        elif rm == 7: parts = ["c->r[R_BX]"]
        if m.disp_size:
            if m.disp_size == 2:
                parts.append(word_at(ins, ctx, m.disp_off, m.disp))
            else:
                parts.append(h16(m.disp))
        expr = " + ".join(parts) if parts else "0"
        seg = default_seg(ins, def_seg)
        self.setup = "const uint16_t s_ = c->seg[%s]; const uint16_t o_ = (uint16_t)(%s);" % (
            SEG_NAMES[seg], expr)

    def read(self, w16):
        m = self.m
        if m.is_reg:
            return "c->r[%s]" % REG16[m.rm] if w16 else "get_r8(c, %d)" % m.rm
        return "seg_read16(c, s_, o_)" if w16 else "mem_read8(c, phys(s_, o_))"

    def write(self, w16, val):
        m = self.m
        if m.is_reg:
            if w16:
                return "c->r[%s] = (uint16_t)(%s);" % (REG16[m.rm], val)
            return "set_r8(c, %d, (uint8_t)(%s));" % (m.rm, val)
        if w16:
            return "seg_write16(c, s_, o_, (uint16_t)(%s));" % val
        return "mem_write8(c, phys(s_, o_), (uint8_t)(%s));" % val


def reg_read(ins, w16):
    r = ins.modrm.reg
    return "c->r[%s]" % REG16[r] if w16 else "get_r8(c, %d)" % r


def reg_write(ins, w16, val):
    r = ins.modrm.reg
    if w16:
        return "c->r[%s] = (uint16_t)(%s);" % (REG16[r], val)
    return "set_r8(c, %d, (uint8_t)(%s));" % (r, val)


STRING_FN = {0xA4: "x86_movs", 0xA5: "x86_movs", 0xAA: "x86_stos", 0xAB: "x86_stos",
             0xAC: "x86_lods", 0xAD: "x86_lods", 0xA6: "x86_cmps", 0xA7: "x86_cmps",
             0xAE: "x86_scas", 0xAF: "x86_scas", 0x6C: "x86_ins", 0x6D: "x86_ins",
             0x6E: "x86_outs", 0x6F: "x86_outs"}


def _emit(ins: Insn, ctx: Ctx):
    """C statements for one instruction, label first."""
    X = ins.ip
    NX = ins.next_ip
    op = ins.op
    w = op & 1
    out = ["L_%04X: CHECK(%s);" % (X, h16(X))]
    body = []
    tail = None        # the control transfer after IC(); None = fall through

    def B(s):
        body.append(s)

    dseg = "c->seg[%s]" % SEG_NAMES[default_seg(ins, S_DS)]

    if ins.kind == K_INVALID:
        out.append("INTERP(%s);" % h16(X))
        return out

    # ---- ALU forms ---------------------------------------------------------
    if op < 0x40 and (op & 7) <= 3 and op not in (0x0F,):
        aop = (op >> 3) & 7
        o = Operand(ins, ctx)
        B("{ " + o.setup)
        if (op & 2) == 0:      # r/m, reg
            B("  const uint16_t r_ = (uint16_t)alu_op(c, %d, %s, %s, %d);" % (aop, o.read(w), reg_read(ins, w), w))
            if aop != 7: B("  " + o.write(w, "r_"))
            else: B("  (void)r_;")
        else:                  # reg, r/m
            B("  const uint16_t r_ = (uint16_t)alu_op(c, %d, %s, %s, %d);" % (aop, reg_read(ins, w), o.read(w), w))
            if aop != 7: B("  " + reg_write(ins, w, "r_"))
            else: B("  (void)r_;")
        B("}")
    elif op < 0x40 and (op & 7) in (4, 5):
        aop = (op >> 3) & 7
        a = "c->r[R_AX]" if w else "get_r8(c, R_AL)"
        B("{ const uint16_t r_ = (uint16_t)alu_op(c, %d, %s, %s, %d);" % (aop, a, imm_expr(ins, ctx), w))
        if aop != 7:
            B("  c->r[R_AX] = r_;" if w else "  set_r8(c, R_AL, (uint8_t)r_);")
        else:
            B("  (void)r_;")
        B("}")
    elif op in (0x06, 0x0E, 0x16, 0x1E):
        B("cpu_push16(c, c->seg[%s]);" % SEG_NAMES[(op >> 3) & 3])
    elif op in (0x07, 0x1F):
        B("c->seg[%s] = cpu_pop16(c);" % SEG_NAMES[(op >> 3) & 3])
    elif op == 0x17:
        B("x86_load_ss(c, cpu_pop16(c));")
    elif op == 0x27: B("x86_daa(c);")
    elif op == 0x2F: B("x86_das(c);")
    elif op == 0x37: B("x86_aaa(c);")
    elif op == 0x3F: B("x86_aas(c);")
    elif 0x40 <= op <= 0x47:
        B("c->r[%s] = (uint16_t)alu_inc(c, c->r[%s], 1);" % (REG16[op & 7], REG16[op & 7]))
    elif 0x48 <= op <= 0x4F:
        B("c->r[%s] = (uint16_t)alu_dec(c, c->r[%s], 1);" % (REG16[op & 7], REG16[op & 7]))
    elif 0x50 <= op <= 0x57:
        B("x86_push_reg(c, %s);" % REG16[op & 7])
    elif 0x58 <= op <= 0x5F:
        B("c->r[%s] = cpu_pop16(c);" % REG16[op & 7])
    elif 0x70 <= op <= 0x7F:
        out.append("IC(); if (x86_cond(c, %d)) %s %s" % (op & 15, transfer(ctx, ins.target), transfer(ctx, NX)))
        return out
    elif op == 0x60: B("x86_pusha(c);")
    elif op == 0x61: B("x86_popa(c);")
    elif op == 0x62:
        o = Operand(ins, ctx)
        B("{ " + o.setup)
        B("  const int16_t idx_ = (int16_t)c->r[%s];" % REG16[ins.modrm.reg])
        B("  const int16_t lo_ = (int16_t)seg_read16(c, s_, o_);")
        B("  const int16_t hi_ = (int16_t)seg_read16(c, s_, (uint16_t)(o_ + 2));")
        B("  if (idx_ < lo_ || idx_ > hi_) { c->ip = %s; c->seg[S_CS] = c->op_cs; cpu_interrupt(c, 5); IC(); return 1; }" % h16(X))
        B("}")
    elif op == 0x68:
        B("cpu_push16(c, %s);" % imm_expr(ins, ctx))
    elif op == 0x6A:
        B("cpu_push16(c, (uint16_t)(int16_t)(int8_t)%s);" % imm_expr(ins, ctx))
    elif op in (0x69, 0x6B):
        o = Operand(ins, ctx)
        imm = imm_expr(ins, ctx) if op == 0x69 else "(uint16_t)(int16_t)(int8_t)%s" % imm_expr(ins, ctx)
        B("{ " + o.setup)
        B("  const uint16_t src_ = %s;" % o.read(1))
        B("  c->r[%s] = x86_imul3(c, src_, %s);" % (REG16[ins.modrm.reg], imm))
        B("}")
    elif op in STRING_FN:
        fn = STRING_FN[op]
        args = "c, %d" % w
        if op in (0xA4, 0xA5, 0xAC, 0xAD, 0xA6, 0xA7, 0x6E, 0x6F):
            args += ", " + dseg
        cmp = op in (0xA6, 0xA7, 0xAE, 0xAF)
        if not ins.rep:
            B("%s(%s);" % (fn, args))
        else:
            # Each iteration is its own instruction boundary (cpu_step points
            # IP back at the prefix), so the loop runs through CHECK again.
            again = "c->r[R_CX] != 0"
            if cmp:
                again += " && ((c->flags & F_ZF) != 0) == %d" % (1 if ins.rep == 0xF3 else 0)
            out.append("if (c->r[R_CX] == 0) { IC(); %s }" % transfer(ctx, NX))
            out.append("if (c->t386) c->t386_elem = 1;")
            out.append("%s(%s);" % (fn, args))
            out.append("c->r[R_CX] = (uint16_t)(c->r[R_CX] - 1);")
            out.append("if (c->t386) c->ip = (%s) ? %s : %s;" % (again, h16(X), h16(NX)))
            out.append("IC(); if (%s) goto L_%04X; %s" % (again, X, transfer(ctx, NX)))
            return out
    elif 0x80 <= op <= 0x83:
        o = Operand(ins, ctx)
        if op == 0x83:
            imm = "(uint16_t)(int16_t)(int8_t)%s" % imm_expr(ins, ctx)
        else:
            imm = imm_expr(ins, ctx)
        wide = 1 if op in (0x81, 0x83) else 0
        B("{ " + o.setup)
        B("  const uint16_t r_ = (uint16_t)alu_op(c, %d, %s, %s, %d);" % (ins.modrm.reg, o.read(wide), imm, wide))
        if ins.modrm.reg != 7: B("  " + o.write(wide, "r_"))
        else: B("  (void)r_;")
        B("}")
    elif op in (0x84, 0x85):
        o = Operand(ins, ctx)
        B("{ " + o.setup + " alu_logic(c, (uint32_t)%s & %s, %d); }" % (o.read(w), reg_read(ins, w), w))
    elif op in (0x86, 0x87):
        o = Operand(ins, ctx)
        B("{ " + o.setup)
        B("  const uint16_t a_ = %s, b_ = %s;" % (o.read(w), reg_read(ins, w)))
        B("  " + o.write(w, "b_"))
        B("  " + reg_write(ins, w, "a_"))
        B("}")
    elif op in (0x88, 0x89):
        o = Operand(ins, ctx)
        B("{ " + o.setup + " " + o.write(w, reg_read(ins, w)) + " }")
    elif op in (0x8A, 0x8B):
        o = Operand(ins, ctx)
        B("{ " + o.setup + " " + reg_write(ins, w, o.read(w)) + " }")
    elif op == 0x8C:
        o = Operand(ins, ctx)
        B("{ " + o.setup + " " + o.write(1, "c->seg[%s]" % SEG_NAMES[ins.modrm.reg & 3]) + " }")
    elif op == 0x8D:
        o = Operand(ins, ctx)
        B("{ " + o.setup + " c->r[%s] = o_; }" % REG16[ins.modrm.reg])
    elif op == 0x8E:
        o = Operand(ins, ctx)
        sr = ins.modrm.reg & 3
        B("{ " + o.setup + " const uint16_t v_ = %s;" % o.read(1))
        if sr == S_SS:
            B("  x86_load_ss(c, v_); }")
        else:
            B("  c->seg[%s] = v_; }" % SEG_NAMES[sr])
            if sr == S_CS:
                tail = "EXIT(%s);" % h16(NX)     # the code segment moved
    elif op == 0x8F:
        o = Operand(ins, ctx)
        B("{ " + o.setup + " " + o.write(1, "cpu_pop16(c)") + " }")
    elif op == 0x90:
        pass
    elif 0x91 <= op <= 0x97:
        B("{ const uint16_t t_ = c->r[R_AX]; c->r[R_AX] = c->r[%s]; c->r[%s] = t_; }" % (REG16[op & 7], REG16[op & 7]))
    elif op == 0x98:
        B("c->r[R_AX] = (uint16_t)(int16_t)(int8_t)get_r8(c, R_AL);")
    elif op == 0x99:
        B("c->r[R_DX] = (c->r[R_AX] & 0x8000) ? 0xFFFF : 0x0000;")
    elif op == 0x9A:
        B("{ const uint16_t noff_ = CODE16(%s), nseg_ = CODE16(%s);" % (h16(X + ins.imm_off), h16(X + ins.imm2_off)))
        B("  cpu_push16(c, c->seg[S_CS]); cpu_push16(c, %s);" % h16(NX))
        B("  c->seg[S_CS] = nseg_; c->ip = noff_; }")
        tail = "return 1;"
    elif op == 0x9B:
        pass
    elif op == 0x9C:
        B("x86_pushf(c);")
    elif op == 0x9D:
        B("x86_popf(c);")
        tail = "if (c->flags & F_TF) { c->ip = %s; cpu_interrupt(c, 1); return 1; } %s" % (h16(NX), transfer(ctx, NX))
    elif op == 0x9E:
        B("x86_sahf(c);")
    elif op == 0x9F:
        B("x86_lahf(c);")
    elif op in (0xA0, 0xA1):
        d = imm_expr(ins, ctx)
        if w: B("c->r[R_AX] = seg_read16(c, %s, %s);" % (dseg, d))
        else: B("set_r8(c, R_AL, mem_read8(c, phys(%s, %s)));" % (dseg, d))
    elif op in (0xA2, 0xA3):
        d = imm_expr(ins, ctx)
        if w: B("seg_write16(c, %s, %s, c->r[R_AX]);" % (dseg, d))
        else: B("mem_write8(c, phys(%s, %s), get_r8(c, R_AL));" % (dseg, d))
    elif op in (0xA8, 0xA9):
        a = "c->r[R_AX]" if w else "get_r8(c, R_AL)"
        B("alu_logic(c, (uint32_t)(%s) & %s, %d);" % (a, imm_expr(ins, ctx), w))
    elif 0xB0 <= op <= 0xB7:
        B("set_r8(c, %d, %s);" % (op & 7, imm_expr(ins, ctx)))
    elif 0xB8 <= op <= 0xBF:
        B("c->r[%s] = %s;" % (REG16[op & 7], imm_expr(ins, ctx)))
    elif op == 0xC2:
        B("{ const uint16_t n_ = %s; c->ip = cpu_pop16(c); c->r[R_SP] = (uint16_t)(c->r[R_SP] + n_); }" % imm_expr(ins, ctx))
        tail = "return 1;"
    elif op == 0xC3:
        B("c->ip = cpu_pop16(c);")
        tail = "return 1;"
    elif op in (0xC0, 0xC1):
        o = Operand(ins, ctx)
        B("{ " + o.setup)
        B("  " + o.write(w, "x86_shift(c, %d, %s, %s, %d)" % (ins.modrm.reg, o.read(w), imm_expr(ins, ctx), w)))
        B("}")
    elif op in (0xC4, 0xC5):
        o = Operand(ins, ctx)
        B("{ " + o.setup)
        B("  c->r[%s] = seg_read16(c, s_, o_);" % REG16[ins.modrm.reg])
        B("  c->seg[%s] = seg_read16(c, s_, (uint16_t)(o_ + 2)); }" % ("S_ES" if op == 0xC4 else "S_DS"))
    elif op in (0xC6, 0xC7):
        o = Operand(ins, ctx)
        B("{ " + o.setup + " " + o.write(w, imm_expr(ins, ctx)) + " }")
    elif op == 0xCA:
        B("{ const uint16_t n_ = %s; c->ip = cpu_pop16(c); c->seg[S_CS] = cpu_pop16(c);" % imm_expr(ins, ctx))
        B("  c->r[R_SP] = (uint16_t)(c->r[R_SP] + n_); }")
        tail = "return 1;"
    elif op == 0xCB:
        B("c->ip = cpu_pop16(c); c->seg[S_CS] = cpu_pop16(c);")
        tail = "return 1;"
    elif op == 0xC8:
        B("x86_enter(c, %s, %s);" % (imm_expr(ins, ctx), byte_at(ins, ctx, ins.imm2_off, ins.imm2)))
    elif op == 0xC9:
        B("x86_leave(c);")
    elif op in (0xCC, 0xCD, 0xCE):
        vec = "3" if op == 0xCC else ("4" if op == 0xCE else imm_expr(ins, ctx))
        call = "c->ip = %s; cpu_interrupt(c, %s);" % (h16(NX), vec)
        back = "if (c->seg[S_CS] != c->op_cs || c->ip != %s || c->halted) return 1; %s" % (h16(NX), transfer(ctx, NX))
        if op == 0xCE:
            out.append("if (c->flags & F_OF) { %s IC(); %s }" % (call, back))
            out.append("IC(); " + transfer(ctx, NX))
        else:
            out.append(call)
            out.append("IC(); " + back)
        return out
    elif op == 0xCF:
        B("x86_iret(c);")
        tail = "if (c->flags & F_TF) cpu_interrupt(c, 1); return 1;"
    elif op in (0xD0, 0xD1, 0xD2, 0xD3):
        o = Operand(ins, ctx)
        cnt = "1" if op < 0xD2 else "get_r8(c, R_CL)"
        B("{ " + o.setup)
        B("  " + o.write(w, "x86_shift(c, %d, %s, %s, %d)" % (ins.modrm.reg, o.read(w), cnt, w)))
        B("}")
    elif op == 0xD4:
        B("c->ip = %s; if (!x86_aam(c, %s)) { IC(); return 1; }" % (h16(NX), imm_expr(ins, ctx)))
    elif op == 0xD5:
        B("x86_aad(c, %s);" % imm_expr(ins, ctx))
    elif op == 0xD6:
        B("set_r8(c, R_AL, (c->flags & F_CF) ? 0xFF : 0x00);")
    elif op == 0xD7:
        B("set_r8(c, R_AL, mem_read8(c, phys(%s, (uint16_t)(c->r[R_BX] + get_r8(c, R_AL)))));" % dseg)
    elif 0xD8 <= op <= 0xDF:
        o = Operand(ins, ctx)
        if not ins.modrm.is_reg:
            B("{ " + o.setup + " (void)mem_read16(c, phys(s_, o_)); }")
    elif op in (0xE0, 0xE1, 0xE2):
        B("c->r[R_CX] = (uint16_t)(c->r[R_CX] - 1);")
        cond = {0xE0: "c->r[R_CX] != 0 && !(c->flags & F_ZF)",
                0xE1: "c->r[R_CX] != 0 && (c->flags & F_ZF)",
                0xE2: "c->r[R_CX] != 0"}[op]
        out.extend(body)
        out.append("IC(); if (%s) %s %s" % (cond, transfer(ctx, ins.target), transfer(ctx, NX)))
        return out
    elif op == 0xE3:
        out.append("IC(); if (c->r[R_CX] == 0) %s %s" % (transfer(ctx, ins.target), transfer(ctx, NX)))
        return out
    elif op in (0xE4, 0xE5):
        B("x86_in(c, %s, %d);" % (imm_expr(ins, ctx), w))
    elif op in (0xE6, 0xE7):
        B("x86_out(c, %s, %d);" % (imm_expr(ins, ctx), w))
    elif op == 0xE8:
        B("cpu_push16(c, %s);" % h16(NX))
        tail = transfer(ctx, ins.target)
    elif op in (0xE9, 0xEB):
        tail = transfer(ctx, ins.target)
    elif op == 0xEA:
        B("{ const uint16_t noff_ = CODE16(%s), nseg_ = CODE16(%s);" % (h16(X + ins.imm_off), h16(X + ins.imm2_off)))
        B("  c->ip = noff_; c->seg[S_CS] = nseg_; }")
        tail = "return 1;"
    elif op in (0xEC, 0xED):
        B("x86_in(c, c->r[R_DX], %d);" % w)
    elif op in (0xEE, 0xEF):
        B("x86_out(c, c->r[R_DX], %d);" % w)
    elif op == 0xF4:
        B("c->halted = 1;")
        tail = "EXIT(%s);" % h16(NX)
    elif op == 0xF5:
        B("c->flags ^= F_CF;")
    elif op in (0xF6, 0xF7):
        o = Operand(ins, ctx)
        reg = ins.modrm.reg
        B("{ " + o.setup + " const uint16_t v_ = %s;" % o.read(w))
        if reg in (0, 1):
            B("  alu_logic(c, (uint32_t)v_ & %s, %d);" % (imm_expr(ins, ctx), w))
        elif reg == 2:
            B("  " + o.write(w, "~v_"))
        elif reg == 3:
            B("  " + o.write(w, "alu_sub(c, 0, v_, %d, 0)" % w))
        elif reg == 4:
            B("  " + ("x86_mul16(c, v_);" if w else "x86_mul8(c, (uint8_t)v_);"))
        elif reg == 5:
            B("  " + ("x86_imul16(c, v_);" if w else "x86_imul8(c, (uint8_t)v_);"))
        elif reg == 6:
            fn = "x86_div16(c, v_)" if w else "x86_div8(c, (uint8_t)v_)"
            B("  c->ip = %s; if (!%s) { IC(); return 1; }" % (h16(NX), fn))
        else:
            fn = "x86_idiv16(c, v_, %d)" % ins.rep if w else "x86_idiv8(c, (uint8_t)v_, %d)" % ins.rep
            B("  c->ip = %s; if (!%s) { IC(); return 1; }" % (h16(NX), fn))
        B("}")
    elif op == 0xF8: B("set_flag(c, F_CF, 0);")
    elif op == 0xF9: B("set_flag(c, F_CF, 1);")
    elif op == 0xFA: B("x86_cli(c);")
    elif op == 0xFB: B("x86_sti(c);")
    elif op == 0xFC: B("set_flag(c, F_DF, 0);")
    elif op == 0xFD: B("set_flag(c, F_DF, 1);")
    elif op == 0xFE:
        o = Operand(ins, ctx)
        f = "alu_inc" if ins.modrm.reg == 0 else "alu_dec"
        B("{ " + o.setup + " const uint16_t v_ = %s; %s }" % (o.read(0), o.write(0, "%s(c, v_, 0)" % f)))
    elif op == 0xFF:
        o = Operand(ins, ctx)
        reg = ins.modrm.reg
        B("{ " + o.setup)
        if reg == 0:
            B("  const uint16_t v_ = %s; %s }" % (o.read(1), o.write(1, "alu_inc(c, v_, 1)")))
        elif reg == 1:
            B("  const uint16_t v_ = %s; %s }" % (o.read(1), o.write(1, "alu_dec(c, v_, 1)")))
        elif reg == 2:
            B("  const uint16_t t_ = %s; cpu_push16(c, %s); c->ip = t_; }" % (o.read(1), h16(NX)))
            tail = "return 1;"
        elif reg == 3:
            B("  const uint16_t noff_ = seg_read16(c, s_, o_);")
            B("  const uint16_t nseg_ = seg_read16(c, s_, (uint16_t)(o_ + 2));")
            B("  cpu_push16(c, c->seg[S_CS]); cpu_push16(c, %s);" % h16(NX))
            B("  c->seg[S_CS] = nseg_; c->ip = noff_; }")
            tail = "return 1;"
        elif reg == 4:
            B("  c->ip = %s; }" % o.read(1))
            tail = "return 1;"
        elif reg == 5:
            B("  c->ip = seg_read16(c, s_, o_);")
            B("  c->seg[S_CS] = seg_read16(c, s_, (uint16_t)(o_ + 2)); }")
            tail = "return 1;"
        else:
            # PUSH r/m16: the 286 reads the operand before SP moves.
            B("  const uint16_t before_ = %s;" % o.read(1))
            B("  c->r[R_SP] = (uint16_t)(c->r[R_SP] - 2);")
            B("  seg_write16(c, c->seg[S_SS], c->r[R_SP], (c->model >= CPU_80286) ? before_ : %s); }" % o.read(1))
    else:
        out.append("INTERP(%s);" % h16(X))
        return out

    out.extend(body)
    if tail is None:
        out.append("IC(); " + transfer(ctx, NX))
    else:
        out.append("IC(); " + tail)
    return out


def emit(ins: Insn, ctx: Ctx):
    """Semantics and the interpreter's timing inputs from the same decode."""
    lines = _emit(ins, ctx)
    if ins.kind != K_INVALID:
        ns = nr = nl = 0
        for byte in ins.raw:
            if byte in (0x26, 0x2E, 0x36, 0x3E): ns += 1
            elif byte in (0xF2, 0xF3): nr += 1
            elif byte in (0xF0, 0xF1): nl += 1
            else: break
        m = ins.modrm
        modrm = (m.mod << 6) | (m.reg << 3) | m.rm if m else -1
        lines.insert(1, "TIMING(%s, %d, %d, %d, %d, %d, %d);" %
                     (h16(ins.next_ip), modrm, ins.seg_ovr, ins.rep, ns, nr, nl))
    return [line.replace("IC()", "IC(%s)" % h8(ins.op)) for line in lines]
