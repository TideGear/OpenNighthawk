#!/usr/bin/env python3
"""probe386.py - cycles per instruction block on 86Box's 386DX/33, against this machine.

    py tools/ref86box/probe386.py OUT_DIR [--profile D:\\86box\\vmt386] [--no-86box] [--engine interp]

A probe program (F117.COM) runs blocks of one instruction class each, with interrupts off, and
writes the block's number to the debug port 0xE9 before each. 86Box (the trace patch's B86_PORTLOG)
logs its cycle counter and instruction count at every such OUT; this machine (F117R_PORTLOG) logs
its clock. A block's cost is the difference between consecutive marks: the marking OUT, the block's
setup and its instructions. The table is the reference the 386DX/33 timing profile is held to
(tools/ref86box/timing386.md); results land in OUT_DIR/probe386.json.
"""
import argparse
import json
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.dirname(HERE))
import probe86  # noqa: E402

BUF = 0x8000           # scratch data in the program's segment
BUF2 = 0x9000


def rep(n, *bs):
    return bytes(bs) * n


def blocks():
    """(name, setup bytes, timed bytes, instructions in the timed part)"""
    b = []

    def add(name, body, count, setup=b""):
        b.append((name, setup, body, count))
    si_buf = bytes([0xBE]) + BUF.to_bytes(2, "little")             # mov si, BUF
    di_buf2 = bytes([0xBF]) + BUF2.to_bytes(2, "little")           # mov di, BUF2
    vga_es = bytes([0xB8, 0x00, 0xA0, 0x8E, 0xC0, 0x31, 0xFF])     # mov ax,A000 / mov es,ax / xor di,di
    add("empty (the mark itself)", b"", 0)
    add("nop x64", rep(64, 0x90), 64)
    add("add r16,r16 x64", rep(64, 0x01, 0xD8), 64)
    add("mov r16,imm16 x64", rep(64, 0xB8, 0x34, 0x12), 64)
    add("mov r16,[si] x64", rep(64, 0x8B, 0x04), 64, si_buf)
    add("mov [si],r16 x64", rep(64, 0x89, 0x04), 64, si_buf)
    add("add [si],r16 x64", rep(64, 0x01, 0x04), 64, si_buf)
    add("cmp r16,[bx+si] x64", rep(64, 0x3B, 0x00), 64, si_buf + bytes([0x31, 0xDB]))
    add("mov r16,[si+disp16] x64", rep(64, 0x8B, 0x84, 0x34, 0x12), 64, si_buf)
    add("push/pop x32", rep(32, 0x50, 0x58), 64)
    add("lodsb x64", rep(64, 0xAC), 64, si_buf)
    add("shl r16,1 x64", rep(64, 0xD1, 0xE0), 64)
    add("shl r16,cl(5) x32", rep(32, 0xD3, 0xE0), 32, bytes([0xB1, 0x05]))
    add("jmp short +0 x64", rep(64, 0xEB, 0x00), 64)
    add("jnz not taken x64", rep(64, 0x75, 0x00), 64, bytes([0x31, 0xC0]))     # xor ax,ax: ZF set
    add("jz taken +0 x64", rep(64, 0x74, 0x00), 64, bytes([0x31, 0xC0]))
    add("loop x64", bytes([0xE2, 0xFE]), 64, bytes([0xB9, 0x40, 0x00]))      # mov cx,64 / l: loop l
    add("mul r16 x32", rep(32, 0xF7, 0xE3), 32, bytes([0xBB, 0x03, 0x00]))
    add("div r16 x32", rep(32, 0xF7, 0xF3), 32, bytes([0xB8, 0xE8, 0x03, 0x31, 0xD2, 0xBB, 0x07, 0x00]))
    add("les r16,[si] x32", rep(32, 0xC4, 0x1C), 32, si_buf)
    add("mov ds,r16 x32", rep(32, 0x8E, 0xD8), 32, bytes([0x8C, 0xD8]))     # mov ax,ds first
    add("rep movsb 200", bytes([0xF3, 0xA4]), 200, si_buf + di_buf2 + bytes([0xB9, 0xC8, 0x00]))
    add("rep stosb 200", bytes([0xF3, 0xAA]), 200, di_buf2 + bytes([0xB9, 0xC8, 0x00]))
    add("rep movsw 200", bytes([0xF3, 0xA5]), 200, si_buf + di_buf2 + bytes([0xB9, 0xC8, 0x00]))
    add("in al,40h x32", rep(32, 0xE4, 0x40), 32)
    add("in al,61h x32", rep(32, 0xE4, 0x61), 32)
    add("in al,dx(388h) x32", rep(32, 0xEC), 32, bytes([0xBA, 0x88, 0x03]))
    add("in al,dx(3DAh) x32", rep(32, 0xEC), 32, bytes([0xBA, 0xDA, 0x03]))
    # VGA memory: ES = A000 for these; restored after.
    add("mov es:[di],al (A000) x32", rep(32, 0x26, 0x88, 0x05), 32, vga_es)
    add("mov al,es:[di] (A000) x32", rep(32, 0x26, 0x8A, 0x05), 32, vga_es)
    add("rep stosb A000 200", bytes([0xF3, 0xAA]), 200, vga_es + bytes([0xB9, 0xC8, 0x00]))
    add("rep stosw A000 200", bytes([0xF3, 0xAB]), 200, vga_es + bytes([0xB9, 0xC8, 0x00]))
    add("rep movsb A000->A000 200", bytes([0x26, 0xF3, 0xA4]), 200,
        vga_es + bytes([0xBE, 0x00, 0x10, 0xB9, 0xC8, 0x00]))                # es: override on the source

    # Calls and interrupts. A body may be a function of its own address (the COM's offset).
    def near_calls(n):
        def body(at):
            out = bytearray([0xEB, 0x01, 0xC3])                           # jmp over / ret
            for k in range(n):
                nxt = 3 + 3 * k + 3
                out += bytes([0xE8]) + ((2 - nxt) & 0xFFFF).to_bytes(2, "little")
            return bytes(out)
        return body

    def far_calls(n):
        def body(at):
            retf = at + 2 + 2
            setup = bytes([0xEB, 0x01, 0xCB])                             # jmp over / retf
            out = bytearray(setup)
            for k in range(n):                                            # push cs / call near retf
                nxt = 3 + 4 * k + 4
                out += bytes([0x0E, 0xE8]) + ((2 - nxt) & 0xFFFF).to_bytes(2, "little")
            return bytes(out)
        return body

    def far_mem_calls(n):
        def body(at):
            out = bytearray([0xEB, 0x01, 0xCB])                           # jmp over / retf at at+2
            for k in range(n):
                out += bytes([0xFF, 0x1E]) + BUF.to_bytes(2, "little")   # call far [BUF]
            return bytes(out)
        return body

    def ints(n):
        def body(at):
            out = bytearray([0xEB, 0x01, 0xCF])                           # jmp over / iret at at+2
            out += bytes([0xCD, 0x60]) * n
            return bytes(out)
        return body
    far_ptr = lambda at: bytes([0xC7, 0x06]) + BUF.to_bytes(2, "little") + (at + 2).to_bytes(2, "little") +         bytes([0x8C, 0x0E]) + (BUF + 2).to_bytes(2, "little")             # mov [BUF],retf / mov [BUF+2],cs
    vec60 = lambda at: bytes([0x1E, 0x31, 0xC0, 0x8E, 0xD8, 0xC7, 0x06, 0x80, 0x01]) + (at + 2).to_bytes(2, "little") +         bytes([0x8C, 0x0E, 0x82, 0x01, 0x1F])                             # push ds / ds=0 / [180h]=iret / [182h]=cs / pop ds
    add("call near/ret x32", near_calls(32), 65)
    add("push cs + call near/retf x32", far_calls(32), 97)
    add("call far [mem]/retf x32", far_mem_calls(32), 65, far_ptr)
    add("int 60h/iret x16", ints(16), 33, vec60)
    add("es: mov r16,[si] x64", rep(64, 0x26, 0x8B, 0x04), 64, si_buf)
    add("cs: rep movsb 200", bytes([0x2E, 0xF3, 0xA4]), 200, si_buf + di_buf2 + bytes([0xB9, 0xC8, 0x00]))
    add("repe cmpsb 50", bytes([0xF3, 0xA6]), 50, si_buf + bytes([0x89, 0xF7, 0xB9, 0x32, 0x00]))   # di=si: all equal
    add("repne scasb 50", bytes([0xF2, 0xAE]), 50, di_buf2 + bytes([0xB0, 0xEE, 0xB9, 0x32, 0x00]))
    add("rep stosb cx=0", bytes([0xF3, 0xAA]), 1, di_buf2 + bytes([0x31, 0xC9]))
    add("movsb x32", rep(32, 0xA4), 32, si_buf + di_buf2)
    add("stosw x32", rep(32, 0xAB), 32, di_buf2)
    add("shl r16,cl(0) x32", rep(32, 0xD3, 0xE0), 32, bytes([0xB1, 0x00]))
    add("shl r16,4 (C1) x32", rep(32, 0xC1, 0xE0, 0x04), 32)
    add("sar [si],1 x32", rep(32, 0xD1, 0x3C), 32, si_buf)
    add("inc r16 x64", rep(64, 0x40), 64)
    add("inc word [si] x32", rep(32, 0xFF, 0x04), 32, si_buf)
    add("add r16,imm8 (83) x64", rep(64, 0x83, 0xC0, 0x05), 64)
    add("add [si],imm8 (83) x32", rep(32, 0x83, 0x04, 0x05), 32, si_buf)
    add("cmp [si+disp8],imm16 x32", rep(32, 0x81, 0x7C, 0x02, 0x34, 0x12), 32, si_buf)
    add("mov [si],imm16 x32", rep(32, 0xC7, 0x04, 0x34, 0x12), 32, si_buf)
    add("mov ax,[moffs] x32", rep(32, 0xA1, 0x00, 0x80), 32)
    add("mov [moffs],ax x32", rep(32, 0xA3, 0x00, 0x80), 32)
    add("xchg ax,r16 x64", rep(64, 0x93), 64)
    add("xchg r16,[si] x32", rep(32, 0x87, 0x1C), 32, si_buf)
    add("imul r16 x32", rep(32, 0xF7, 0xEB), 32, bytes([0xBB, 0x03, 0x00]))
    add("imul r16,r16,imm8 x32", rep(32, 0x6B, 0xC3, 0x07), 32)
    add("cwd/idiv r16 x32", rep(32, 0x99, 0xF7, 0xFB), 64, bytes([0xB8, 0xE8, 0x03, 0xBB, 0x07, 0x00]))
    add("neg r16 x64", rep(64, 0xF7, 0xD8), 64)
    add("cbw/cwd x32", rep(32, 0x98, 0x99), 64)
    add("xlat x32", rep(32, 0xD7), 32, bytes([0xBB]) + BUF.to_bytes(2, "little"))
    add("push imm16 / pop x32", rep(32, 0x68, 0x34, 0x12, 0x58), 64)
    add("pusha/popa x16", rep(16, 0x60, 0x61), 32)
    add("enter 4,0/leave x16", rep(16, 0xC8, 0x04, 0x00, 0x00, 0xC9), 32)
    add("lea r16,[bx+si+disp8] x64", rep(64, 0x8D, 0x40, 0x10), 64)
    add("test r16,imm16 x64", rep(64, 0xA9, 0x34, 0x12), 64)
    add("clc/stc/cld x32", rep(32, 0xF8, 0xF9, 0xFC), 96)
    add("lahf/sahf x32", rep(32, 0x9F, 0x9E), 64)
    add("pushf/popf x32", rep(32, 0x9C, 0x9D), 64)
    add("jcxz not taken x64", rep(64, 0xE3, 0x00), 64, bytes([0xB9, 0x01, 0x00]))
    add("out dx,al (3C8h) x32", rep(32, 0xEE), 32, bytes([0xBA, 0xC8, 0x03, 0xB0, 0x00]))
    add("out 21h,al x32", rep(32, 0xE6, 0x21), 32, bytes([0xE4, 0x21]))
    add("in al,dx (201h unclaimed) x32", rep(32, 0xEC), 32, bytes([0xBA, 0x01, 0x02]))
    add("mov sreg pop ss pair x16", rep(16, 0x16, 0x17), 32)
    add("aam/aad x16", rep(16, 0xD4, 0x0A, 0xD5, 0x0A), 32)
    return b


def build():
    code = bytearray()
    org = 0x100

    def emit(bs):
        code.extend(bs)
    emit([0xB8, 0x13, 0x00, 0xCD, 0x10])                # mov ax,13h / int 10h: mode 13h, so A000 is the VGA's
    emit([0xFA, 0xFC])                                   # cli, cld
    emit([0x8C, 0xC8, 0x8E, 0xD8, 0x8E, 0xC0])          # mov ax,cs / mov ds,ax / mov es,ax
    names = []
    for i, (name, setup, body, count) in enumerate(blocks()):
        names.append((name, count))
        emit([0xB0, i, 0xE6, 0xE9])                      # mov al,i / out E9h,al
        if callable(setup) or callable(body):
            # a setup that names the body's address: its length does not depend on it
            slen = len(setup(0)) if callable(setup) else len(setup)
            at = org + len(code) + slen
            emit(setup(at) if callable(setup) else setup)
            emit(body(at) if callable(body) else body)
        else:
            emit(setup)
            emit(body)
        emit([0x8C, 0xC8, 0x8E, 0xC0, 0x8E, 0xD8])      # mov ax,cs / mov es,ax / mov ds,ax (restore)
    emit([0xB0, 0xFF, 0xE6, 0xE9])                       # the end mark
    emit([0xFB, 0xB8, 0x03, 0x00, 0xCD, 0x10])          # sti / mov ax,3 / int 10h: text mode again
    emit([0xB8, 0x00, 0x4C, 0xCD, 0x21])                # mov ax,4C00 / int 21h
    assert org + len(code) < BUF
    return bytes(code), names


def marks_86box(path):
    rows = [list(map(int, l.split())) for l in open(path) if l.strip()]
    return [(tsc, ins, val) for tsc, ins, val in rows]


def marks_ours(path):
    return [(clk, None, val) for clk, val in (map(int, l.split()) for l in open(path) if l.strip())]


def costs(marks, names):
    """Per block: clocks from its mark to the next, and instructions where known."""
    by = {}
    for (t0, i0, v0), (t1, i1, v1) in zip(marks, marks[1:]):
        if v0 < len(names) and v1 in (v0 + 1, 0xFF):
            by[v0] = (t1 - t0, None if i0 is None else i1 - i0)
    return by


def run_86box(out, profile, probe, frames=3000, timeout=600):
    work = os.path.normpath(os.path.join(out, "profile"))
    shutil.rmtree(work, ignore_errors=True)
    os.makedirs(work)
    shutil.copyfile(os.path.join(profile, "86box.cfg"), os.path.join(work, "86box.cfg"))
    shutil.copytree(os.path.join(profile, "nvr"), os.path.join(work, "nvr"))
    subprocess.run(["cmd", "/c", "mklink", "/J", os.path.join(work, "roms"), r"D:\86box\app\roms"],
                   stdout=subprocess.DEVNULL, check=True)
    img = os.path.join(work, "f117a.img")
    shutil.copyfile(os.path.join(profile, "f117a.img"), img)

    def install(fs):
        fs.writebytes("/F117A/F117.COM", probe)
        bat = fs.readtext("/FDAUTO.BAT").replace("\r\n", "\n")
        lines = [l for l in bat.split("\n") if l.strip() and l.strip().upper() != "F117"] + ["F117"]
        fs.writetext("/FDAUTO.BAT", "\r\n".join(lines) + "\r\n")
    probe86.with_partition(img, install, write=True)
    log = os.path.join(out, "86box-ports.log")
    if os.path.exists(log):
        os.remove(log)
    env = dict(os.environ, B86_PORTLOG=log)
    subprocess.run(["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File",
                    os.path.join(HERE, "trace_86box.ps1"), "-Profile", work, "-Out", os.path.join(out, "trace"),
                    "-Stop", str(frames), "-TimeoutSeconds", str(timeout)], check=True, env=env)
    return log


def run_ours(out, probe, engine, timing=None):
    d = os.path.join(out, "ours")
    os.makedirs(d, exist_ok=True)
    open(os.path.join(d, "F117.COM"), "wb").write(probe)
    log = os.path.join(out, "ours-ports.log")
    if os.path.exists(log):
        os.remove(log)
    env = dict(os.environ, F117R_PORTLOG=log)
    subprocess.run([os.path.join(ROOT, "build", "f117run.exe"), "--engine", engine, "--data", d]
                   + (["--timing", timing] if timing else []) + [
                    "--save", os.path.join(out, "ours_save"), "--steps", "50000000",
                    "--log", os.path.join(out, "ours.log")], env=env, capture_output=True, timeout=300)
    return log


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("out")
    ap.add_argument("--profile", default=r"D:\86box\vmt386")
    ap.add_argument("--no-86box", action="store_true", help="reuse OUT_DIR/86box-ports.log")
    ap.add_argument("--engine", default="interp", choices=("interp", "recomp"))
    ap.add_argument("--timing", choices=("386",), help="run this machine under the 386DX/33 profile")
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    probe, names = build()
    box_log = os.path.join(a.out, "86box-ports.log") if a.no_86box else run_86box(a.out, a.profile, probe)
    box = costs(marks_86box(box_log), names)
    ours = costs(marks_ours(run_ours(a.out, probe, a.engine, a.timing)), names)
    rows = []
    print("%-28s %8s %6s %8s %8s %6s" % ("block", "86Box", "ins", "per ins", "ours", "diff"))
    for i, (name, count) in enumerate(names):
        bc, bi = box.get(i, (None, None))
        oc = ours.get(i, (None, None))[0]
        rows.append(dict(block=name, count=count, box_cycles=bc, box_ins=bi, ours=oc))
        print("%-28s %8s %6s %8s %8s %6s" % (name, bc, bi, "" if bc is None or not count else "%.2f" % (bc / count), oc,
                                            "" if bc is None or oc is None else oc - bc))
    json.dump(rows, open(os.path.join(a.out, "probe386.json"), "w"), indent=1)
    missing = [r["block"] for r in rows if r["box_cycles"] is None or r["ours"] is None]
    if missing:
        print("no measurement for:", ", ".join(missing))
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
