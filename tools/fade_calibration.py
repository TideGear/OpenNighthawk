#!/usr/bin/env python3
"""Run the game's original fade calibrators on GOG DOSBox and this runtime.

START/END count palette writes in four BIOS ticks; PLAYER counts DAC bytes
per display period. These phase-dependent samples are diagnostics, not an
exact parity verdict. Extracted game bytes and all results stay outside the
repository. Requires the user's game installation, not the Reimp project.
"""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT))
from recompiler.modules import load_module
from fidelity import Asm

RESULTS, FILENAME, PALETTE = 0x2700, 0x2800, 0x6000
PROFILES = {
    "START": (0x31AD, 0xB304, 1, ((0x31AD, 0x31FA), (0x8364, 0x8440)), 0xB400),
    "PLAYER": (0xDE4, 0x13D2, 2, ((0xDE4, 0xE0C),), 0x2A00),
    "END": (0x1708, 0x55BA, 1, ((0x1708, 0x1755), (0x42D0, 0x4393)), 0x6300),
}


def build_probe(image: bytes, name: str) -> bytes:
    entry, count, width, ranges, end = PROFILES[name]
    if any(hi > len(image) for _, hi in ranges):
        raise ValueError(f"{name}: unpacked image is too short")
    a = Asm()
    a.mov_r16_imm("ax", 0x13); a.int_(0x10)
    a.mov_r16_imm("si", 0)
    a.label("again")
    a.db(0xC6 if width == 1 else 0xC7, 0x06); a.dw(count)
    if width == 1:
        a.db(0)
    else:
        a.dw(0)
    a.mov_r16_imm("ax", PALETTE); a.push("ax")
    a.labels["calibrator"] = entry
    a.call("calibrator"); a.pop("ax")
    if width == 1:
        a.db(0xA0); a.dw(count)  # mov al, [count]
        a.mov_r8_imm("ah", 0)
    else:
        a.db(0xA1); a.dw(count)  # mov ax, [count]
    a.db(0x89, 0x84); a.dw(RESULTS)  # mov [si + RESULTS], ax
    a.add_r16_imm("si", 2); a.cmp_r16_imm("si", 10)
    a.jcc("jb", "again")
    a.mov_r16_imm("dx", FILENAME); a.xor_rr16("cx", "cx")
    a.mov_r8_imm("ah", 0x3C); a.int_(0x21)
    a.jcc("jc", "failed")
    a.mov_rr16("bx", "ax")
    a.mov_r16_imm("dx", RESULTS); a.mov_r16_imm("cx", 10)
    a.mov_r8_imm("ah", 0x40); a.int_(0x21)
    a.jcc("jc", "failed")
    a.cmp_r16_imm("ax", 10); a.jcc("jne", "failed")
    a.mov_r8_imm("ah", 0x3E); a.int_(0x21)
    a.mov_r16_imm("ax", 0x4C00); a.int_(0x21)
    a.label("failed")
    a.mov_r16_imm("ax", 0x4C01); a.int_(0x21)
    code = a.link()
    if 0x100 + len(code) >= min(lo for lo, _ in ranges):
        raise ValueError("probe overlaps a copied routine")
    result = bytearray(end - 0x100)
    result[:len(code)] = code
    for lo, hi in ranges:
        result[lo - 0x100:hi - 0x100] = image[lo:hi]
    result[FILENAME - 0x100:FILENAME - 0x100 + 8] = b"OUT.BIN\0"
    if name != "PLAYER":
        result[PALETTE - 0x100:PALETTE - 0x100 + 768] = bytes(i % 64 for i in range(768))
    return bytes(result)


def read_samples(path: Path) -> list[int]:
    raw = path.read_bytes()
    if len(raw) != 10:
        raise ValueError(f"{path}: expected five calibration words, got {len(raw)} bytes")
    return list(struct.unpack("<5H", raw))


def compare(data: Path, work: Path, name: str, probe: bytes, trial: int) -> dict:
    run = work / name / str(trial)
    reference, current = run / "dosbox", run / "ours"
    reference.mkdir(parents=True); current.mkdir()
    for folder in (reference, current):
        (folder / "F117.COM").write_bytes(probe)
    conf = run / "probe.conf"
    conf.write_text('[sdl]\nfullscreen=false\noutput=surface\n[autoexec]\n'
                    f'@echo off\nmount C "{reference}"\nc:\nkeyb us\ncls\nf117\nexit\n')
    dosbox = data / "DOSBOX" / "DOSBox.exe"
    env = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
    r = subprocess.run([str(dosbox), "-conf", str(data / "dosboxF117A.conf"),
                        "-conf", str(conf), "-noconsole"], cwd=dosbox.parent,
                       env=env, timeout=60, capture_output=True, text=True)
    (run / "dosbox.txt").write_text(r.stdout + r.stderr)
    r.check_returncode()
    r = subprocess.run([str(ROOT / "build" / "f117run.exe"), "--engine", "interp",
                        "--data", str(current), "--save", str(run / "save"),
                        "--log", str(run / "run.log"), "--steps", "40000000",
                        "--time-us", "700000000000000"], timeout=60,
                       capture_output=True, text=True)
    (run / "runner.txt").write_text(r.stdout + r.stderr)
    r.check_returncode()
    return {"dosbox": read_samples(reference / "OUT.BIN"),
            "ours": read_samples(run / "save" / "OUT.BIN")}


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--data", required=True, type=Path)
    ap.add_argument("--trials", type=int, default=3)
    ap.add_argument("--program", choices=tuple(PROFILES), action="append")
    ap.add_argument("--work", type=Path, default=Path.home() / "f117-recomp-local" / "fadecal")
    args = ap.parse_args()
    if args.trials < 1:
        ap.error("--trials must be positive")
    args.work.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix="calibration-", dir=args.work)).resolve()
    report = {"data": str(args.data.resolve()), "trials": args.trials,
              "diagnostic_only": True, "programs": {}}
    print(f"Artifacts: {work}", flush=True)
    for name in dict.fromkeys(args.program or PROFILES):
        module = load_module(str(args.data), name + ".EXE")
        if module is None:
            raise ValueError(f"missing module: {name}")
        probe = build_probe(module.image, name)
        samples = [compare(args.data.resolve(), work, name, probe, i)
                   for i in range(args.trials)]
        report["programs"][name] = samples
        print(f"{name} ({'DAC bytes/display period' if name == 'PLAYER' else 'palette writes/four BIOS ticks'}):", flush=True)
        for i, sample in enumerate(samples):
            print(f"  {i}: DOSBox {sample['dosbox']}  ours {sample['ours']}", flush=True)
        (work / "comparison.json").write_text(json.dumps(report, indent=2) + "\n")
    print("Phase-dependent diagnostics; these samples do not assert exact parity.")


if __name__ == "__main__":
    main()
