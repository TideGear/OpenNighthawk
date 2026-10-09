#!/usr/bin/env python3
"""sst_recomp.py - the recompiler's output, held to real silicon.

The vector harnesses (sstest.py: 8088, sst286.py: 80286 real mode) prove the
interpreter. This proves the TRANSLATOR: every selected vector's instruction
is decoded by recompiler/x86dec.py and emitted by recompiler/emit.py exactly
as game code is, the C is compiled into a test library with the CPU core,
and the vector runs through the generated function instead of cpu_step. The
comparison - registers, flags (minus the documented undefined bits), memory,
and the named buckets of sst286.py - is the harnesses' own, unchanged.

    py tests/sst_recomp.py [--per-file N] [--8088-only | --286-only] [--verbose]

Needs the vector caches the other two harnesses use (F117R_SST_CACHE,
F117R_SST286_DIR) and MSVC (vcvars64.bat).
"""
from __future__ import annotations

import ctypes
import gzip
import json
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "recompiler"))
sys.path.insert(0, HERE)

import sstest                     # noqa: E402
import sst286                     # noqa: E402
from emit import Ctx, emit        # noqa: E402
from x86dec import DecodeError, decode   # noqa: E402

VCVARS = r"C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
# Opcodes whose DECODING differs between the 8086 and the 286 (the
# recompiler decodes for the 286, the machine's CPU): the 8088 vectors for
# these cannot apply, and the 286 vectors cover them.
NOT_ON_8088 = set(range(0x60, 0x70)) | {0xC0, 0xC1, 0xC8, 0xC9, 0x0F}
STRING_OPS = set(range(0xA4, 0xA8)) | set(range(0xAA, 0xB0)) | {0x6C, 0x6D, 0x6E, 0x6F}


class Gen:
    """Collects one generated function per distinct (ip, instruction bytes)."""

    def __init__(self):
        self.key = {}
        self.funcs = []

    def add(self, ram_at, ip):
        code = bytes(ram_at(k) for k in range(16))
        try:
            ins = decode(code, 0, ip, 16)
        except DecodeError:
            return None
        k = (ip, ins.raw)
        if k in self.key:
            return self.key[k]
        live = set()
        if ins.op in (0x9A, 0xEA):
            live = set(range(ins.imm_off, ins.imm_off + 4))
        lines = emit(ins, Ctx(lambda x, ip=ip: x == ip, frozenset(live)))
        rep = int(bool(ins.rep) and ins.op in STRING_OPS)
        idx = len(self.funcs)
        self.funcs.append((ip, lines, rep))
        self.key[k] = idx
        return idx

    def write(self, dirpath, per_file=400):
        files = []
        for start in range(0, len(self.funcs), per_file):
            p = os.path.join(dirpath, "sst_gen_%03d.c" % (start // per_file))
            with open(p, "w", newline="\n") as f:
                f.write('#include "recomp_gen.h"\n#pragma warning(disable: 4102 4127 4189 4244 4310 4702)\n')
                for i in range(start, min(start + per_file, len(self.funcs))):
                    ip, lines, _ = self.funcs[i]
                    f.write("int T_%d(cpu_t *c)\n{\n    c->op_cs = c->seg[S_CS];\n" % i)
                    f.write("    switch (c->ip) { case 0x%04X: goto L_%04X; default: return 0; }\n" % (ip, ip))
                    for ln in lines:
                        f.write("    " + ln + "\n")
                    f.write("    return 1;\n}\n")
            files.append(p)
        p = os.path.join(dirpath, "sst_gen_tab.c")
        with open(p, "w", newline="\n") as f:
            f.write('#include "recomp_gen.h"\n')
            for i in range(len(self.funcs)):
                f.write("int T_%d(cpu_t *c);\n" % i)
            f.write("const rc_fn GEN_TESTS[] = {\n")
            for i in range(len(self.funcs)):
                f.write("  T_%d,\n" % i)
            f.write("};\nconst unsigned char GEN_REP[] = {\n")
            for i in range(len(self.funcs)):
                f.write("%d," % self.funcs[i][2])
            f.write("0};\n")
        files.append(p)
        return files


API_EXTRA = r'''
#include "recomp_gen.h"
int rc_instruction_budget = -1;
extern const rc_fn GEN_TESTS[];
extern const unsigned char GEN_REP[];
typedef struct { cpu_t cpu; uint8_t *mem; } oracle_t;
static long g_interp;
/* One vector through its generated function: the whole instruction (a REP
 * string op to retirement). A translation that hands back to the
 * interpreter (an invalid encoding) is counted. */
__declspec(dllexport) int orc_run_gen(oracle_t *o, int idx)
{
    cpu_t *c = &o->cpu;
    c->stop_at = c->icount + (GEN_REP[idx] ? 70000u : 1u);
    if (!GEN_TESTS[idx](c)) { g_interp++; return cpu_step(c); }
    return 0;
}
__declspec(dllexport) long orc_gen_interpreted(void) { return g_interp; }
'''


def build(files, outdir):
    api = os.path.join(outdir, "sst_api_extra.c")
    open(api, "w").write(API_EXTRA)
    srcs = [os.path.join(ROOT, "src", "cpu", "cpu.c"), os.path.join(ROOT, "src", "cpu", "timing386.c"),
            os.path.join(ROOT, "tests", "cpu_api.c"), api] + files
    dll = os.path.join(outdir, "sst_recomp.dll")
    inc = '/I"%s" /I"%s"' % (os.path.join(ROOT, "src", "cpu"), os.path.join(ROOT, "src", "recomp"))
    rsp = os.path.join(outdir, "cl.rsp")
    with open(rsp, "w") as f:
        f.write("/nologo /MP /O1 /W0 /LD %s\n" % inc)
        for s in srcs:
            f.write('"%s"\n' % s)
        f.write('/Fe:"%s"\n' % dll)
    bat = os.path.join(outdir, "build.bat")
    with open(bat, "w") as f:
        f.write('@echo off\r\ncall "%s" >nul\r\ncd /d "%s"\r\ncl @cl.rsp >build.log 2>&1\r\n' % (VCVARS, outdir))
    r = subprocess.run([bat], capture_output=True, text=True, shell=True)
    if r.returncode != 0 or not os.path.exists(dll):
        log = os.path.join(outdir, "build.log")
        print(open(log).read()[-3000:] if os.path.exists(log) else r.stdout + r.stderr)
        sys.exit("build failed")
    return dll


def make_stepper(base_cls, lib_path, gen):
    class GenOracle(base_cls):
        def __init__(self):
            super().__init__()
            L = ctypes.CDLL(lib_path)
            L.orc_new.restype = ctypes.c_void_p
            L.orc_run_gen.argtypes = [ctypes.c_void_p, ctypes.c_int]
            L.orc_run_gen.restype = ctypes.c_int
            self.lib = L
            # the base class bound its functions to the default DLL; rebind
            for fn, args in (("orc_free", [ctypes.c_void_p]), ("orc_reset", [ctypes.c_void_p]),
                             ("orc_set_reg", [ctypes.c_void_p, ctypes.c_int, ctypes.c_uint32]),
                             ("orc_write8", [ctypes.c_void_p, ctypes.c_uint32, ctypes.c_uint8]),
                             ("orc_set_model", [ctypes.c_void_p, ctypes.c_int])):
                getattr(L, fn).argtypes = args
            L.orc_get_reg.argtypes = [ctypes.c_void_p, ctypes.c_int]
            L.orc_get_reg.restype = ctypes.c_uint32
            L.orc_read8.argtypes = [ctypes.c_void_p, ctypes.c_uint32]
            L.orc_read8.restype = ctypes.c_uint8
            self.h = L.orc_new()
            self.model = 86
            self.missing = 0

        def reset(self):
            self.lib.orc_reset(self.h)
            self.lib.orc_set_model(self.h, self.model)

        def step(self):
            cs, ip = self.get("cs"), self.get("ip")
            code = bytes(self.r8(((cs << 4) + ((ip + k) & 0xFFFF)) & 0xFFFFF) for k in range(16))
            try:
                ins = decode(code, 0, ip, 16)
            except DecodeError:
                self.missing += 1
                return -1
            idx = gen.key.get((ip, ins.raw))
            if idx is None:
                self.missing += 1
                return -1
            return self.lib.orc_run_gen(self.h, idx)
    return GenOracle


def select_8088(per_file):
    out = []
    for fname, op, sub in sstest.cached_files(set(range(256)) - NOT_ON_8088):
        out.append((fname, op, sub))
    return out


def gen_8088(gen, files, per_file):
    for fname, op, sub in files:
        with gzip.open(os.path.join(sstest.CACHE, fname), "rt", encoding="utf-8") as fh:
            tests = json.load(fh)
        for t in tests[:per_file]:
            ram = {a & 0xFFFFF: v for a, v in t["initial"].get("ram", [])}
            r = t["initial"]["regs"]
            cs, ip = r["cs"], r["ip"]
            gen.add(lambda k: ram.get(((cs << 4) + ((ip + k) & 0xFFFF)) & 0xFFFFF, 0), ip)


def gen_286(gen, d, per_file):
    files = []
    for f in sorted(os.listdir(d)):
        m = sst286.FILE_RE.match(f)
        if not m or len(m.group(1)) == 4:
            continue
        files.append((f, m.group(1) + ("." + m.group(2) if m.group(2) else "")))
        for name, byts, ireg, iram, freg, fram, exc in sst286.read_tests(os.path.join(d, f), per_file):
            cells = {a & 0xFFFFF: v for a, v in iram}
            cs, ip = ireg.get("cs", 0), ireg.get("ip", 0)
            gen.add(lambda k: cells.get(((cs << 4) + ((ip + k) & 0xFFFF)) & 0xFFFFF, 0), ip)
    return files


def main():
    flags = sys.argv[1:]
    per_file = next((int(f.split("=")[1]) for f in flags if f.startswith("--per-file=")), 60)
    verbose = "--verbose" in flags
    do88 = "--286-only" not in flags
    do286 = "--8088-only" not in flags
    d286 = os.environ.get("F117R_SST286_DIR") or sst286.DEFAULT_DIR

    gen = Gen()
    files88 = select_8088(per_file) if do88 else []
    if do88:
        gen_8088(gen, files88, per_file)
    files286 = gen_286(gen, d286, per_file) if do286 and os.path.isdir(d286) else []
    print("generated %d distinct instruction translations" % len(gen.funcs))
    outdir = tempfile.mkdtemp(prefix="sst_recomp_")
    dll = build(gen.write(outdir), outdir)
    print("built %s" % dll)

    rc = 0
    if do88:
        O = make_stepper(sstest.Oracle, dll, gen)
        orc = O()
        orc.model = 86
        total = tfail = 0
        for fname, op, sub in files88:
            n, fails = sstest.run_file(orc, os.path.join(sstest.CACHE, fname), op, sub, per_file)
            total += n
            tfail += len(fails)
            if fails:
                print("8088 %-8s FAIL %d/%d" % (fname[:-8], len(fails), n))
                if verbose:
                    for name, bts, bad, r in fails[:3]:
                        print("      [%s] rc=%d %s" % (bts, r, "; ".join(bad[:4])))
        print("8088 vectors through generated code: %d tests, %d failures, %d with no translation"
              % (total, tfail, orc.missing))
        rc |= tfail != 0
    if do286 and files286:
        O = make_stepper(sst286.Oracle, dll, gen)
        orc = O()
        orc.model = 286
        total = unexpl = expl = 0
        for f, label in files286:
            n, w, fails = sst286.run_file(orc, os.path.join(d286, f), per_file)
            total += n
            bad = [x for x in fails if x[5] is None]
            expl += len(fails) - len(bad)
            unexpl += len(bad)
            if bad:
                print("286  %-8s UNEXPLAINED %d/%d" % (label, len(bad), n))
                if verbose:
                    for nm, bts, b, r, exc, _ in bad[:3]:
                        print("      [%s] rc=%d %s" % (bts, r, "; ".join(b[:4])))
        print("286 vectors through generated code: %d tests, %d agree, %d explained by a named bucket, "
              "%d UNEXPLAINED, %d with no translation" % (total, total - expl - unexpl, expl, unexpl, orc.missing))
        rc |= unexpl != 0
    return rc


if __name__ == "__main__":
    sys.exit(main())
