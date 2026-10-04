#!/usr/bin/env python3
"""ROM-free OPL tone/capture diagnostic against the user's GOG DOSBox.

Synthesizes an original COM program, never copies game bytes. Normal posted
keys start WAV capture and release the probe's waiting keyboard read. Outputs
stay in --out. Requires pywin32, NumPy and the application build.
"""
import argparse
import json
import os
from pathlib import Path
import subprocess
import time
import wave

import numpy as np
from fidelity import Asm

ROOT = Path(__file__).resolve().parents[1]


def probe():
    a = Asm(); a.db(0x0e, 0x1f); a.sti()
    registers = [(0x20, 0x21), (0x23, 0x21), (0x40, 0x10), (0x43, 0),
                 (0x60, 0xf0), (0x63, 0xf0), (0x80, 0x0f), (0x83, 0x0f),
                 (0xc0, 0), (0xa0, 0x98), (0xb0, 0x11)]
    for reg, val in registers:
        a.mov_r16_imm("ax", val << 8 | reg); a.call("write_opl")
    a.xor_rr16("ax", "ax"); a.int_(0x16)
    for value, ticks in ((0x31b0, 36), (0x40a0, 36), (0x11b0, 18)):
        a.mov_r16_imm("ax", value); a.call("write_opl")
        a.mov_r16_imm("cx", ticks); a.call("delay")
    a.mov_r16_imm("ax", 0x4c00); a.int_(0x21)
    a.label("write_opl"); a.push("cx"); a.mov_r16_imm("dx", 0x388); a.out_dx_al()
    a.mov_r16_imm("cx", 6); a.label("address_delay"); a.db(0xec); a.loop("address_delay")
    a.inc16("dx"); a.mov_rr8("al", "ah"); a.out_dx_al(); a.dec16("dx")
    a.mov_r16_imm("cx", 35); a.label("data_delay"); a.db(0xec); a.loop("data_delay")
    a.pop("cx"); a.ret()
    a.label("delay"); a.db(0x06); a.mov_r16_imm("ax", 0x40); a.mov_sreg_r16("es", "ax")
    a.load_es_abs("bx", 0x6c); a.label("tick"); a.db(0xf4); a.load_es_abs("ax", 0x6c)
    a.sub_rr16("ax", "bx"); a.db(0x39, 0xc8); a.jcc("jb", "tick"); a.db(0x07); a.ret()
    return a.link()


def capture(data, out):
    import win32api, win32con as wc, win32gui, win32process
    cap = out / "capture"; cap.mkdir()
    conf = out / "probe.conf"
    conf.write_text(f"[sdl]\nfullscreen=false\noutput=surface\n[dosbox]\ncaptures={cap}\n"
                    f'[autoexec]\n@echo off\nmount c "{out / "game"}"\nc:\nkeyb us\ncls\nf117\nexit\n')
    startup = subprocess.STARTUPINFO(); startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    startup.wShowWindow = 7
    exe = data / "DOSBOX" / "DOSBox.exe"
    process = subprocess.Popen([str(exe), "-conf", str(data / "dosboxF117A.conf"),
        "-conf", str(conf), "-noconsole"], cwd=out, startupinfo=startup,
        env=dict(os.environ, SDL_VIDEODRIVER="windib"))
    try:
        hwnd = None
        deadline = time.monotonic() + 15
        while time.monotonic() < deadline:
            found = []
            def window(h, _):
                if win32process.GetWindowThreadProcessId(h)[1] == process.pid and win32gui.GetWindowText(h).split("Program:")[-1].strip() == "F117":
                    found.append(h)
            win32gui.EnumWindows(window, None)
            if found: hwnd = found[0]; break
            if process.poll() is not None: raise RuntimeError("DOSBox closed before probe")
            time.sleep(.05)
        if hwnd is None: raise RuntimeError("probe window not found")
        def key(vk, down):
            scan = win32api.MapVirtualKey(vk, 0)
            win32gui.PostMessage(hwnd, wc.WM_KEYDOWN if down else wc.WM_KEYUP, vk,
                                 1 | scan << 16 | (0 if down else 3 << 30))
        time.sleep(.2)
        key(wc.VK_CONTROL, True); key(wc.VK_F6, True); time.sleep(.1)
        key(wc.VK_F6, False); key(wc.VK_CONTROL, False); time.sleep(.5)
        key(wc.VK_SPACE, True); time.sleep(.1); key(wc.VK_SPACE, False)
        process.wait(timeout=15)
        if process.returncode: raise RuntimeError(f"DOSBox exited {process.returncode}")
    finally:
        if process.poll() is None: process.terminate(); process.wait(timeout=10)
    files = sorted(cap.glob("*.wav"))
    if len(files) != 1: raise RuntimeError(f"expected one WAV capture, found {files}")
    return files[0]


def pcm(path):
    with wave.open(str(path), "rb") as sound:
        if sound.getsampwidth() != 2 or sound.getframerate() != 44100:
            raise ValueError("probe requires 16-bit 44100 Hz output")
        return np.frombuffer(sound.readframes(sound.getnframes()), "<i2").reshape(-1, sound.getnchannels())


def compare(reference, ours):
    a, b = pcm(reference), pcm(ours)
    if a.shape[1] != 2 or b.shape[1] != 2: raise ValueError("expected stereo")
    starts = [np.flatnonzero(np.any(x != 0, axis=1)) for x in (a, b)]
    if any(not len(x) for x in starts): raise ValueError("silent probe")
    a, b = a[starts[0][0]:], b[starts[1][0]:]
    count = min(len(a), len(b)); a, b = a[:count], b[:count]
    if count < 44100 * 4: raise ValueError("at least four seconds after sound onset required")
    differences = np.abs(a.astype(np.int32) - b.astype(np.int32))
    unequal = np.flatnonzero(np.any(a != b, axis=1))
    return {"reference_first_sound_frame": int(starts[0][0]),
            "ours_first_sound_frame": int(starts[1][0]), "frames": count,
            "exact_pcm_equal": bool(np.array_equal(a, b)),
            "equal_frames": int(np.count_nonzero(np.all(a == b, axis=1))),
            "equal_prefix_frames": int(unequal[0]) if len(unequal) else count,
            "max_sample_difference": int(differences.max()),
            "first_second_equal_frames": int(np.count_nonzero(np.all(a[:44100] == b[:44100], axis=1))),
            "reference_peak": int(np.max(np.abs(a.astype(np.int32)))),
            "ours_peak": int(np.max(np.abs(b.astype(np.int32)))),
            "limitations": "first nonzero frame alignment; BIOS-tick duration phases may differ"}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--data", required=True)
    parser.add_argument("--out", required=True)
    args = parser.parse_args()
    out = Path(args.out).resolve(); out.mkdir(parents=True, exist_ok=False)
    game = out / "game"; game.mkdir(); (game / "F117.COM").write_bytes(probe())
    reference = capture(Path(args.data).resolve(), out)
    subprocess.run([str(ROOT / "build" / "f117run.exe"), "--data", str(game),
        "--save", str(out / "save"), "--engine", "interp", "--steps", "70000000",
        "--type", r"F117.COM+9000000: ", "--opl-log", str(out / "opl.log"),
        "--log", str(out / "run.log")], check=True, stdout=subprocess.DEVNULL)
    # Include the stopped tail through seven seconds; compare the common
    # duration after the first audible sample, retaining all unequal frames.
    reports = {}
    for backend in ("dbopl", "nuked"):
        output = out / f"{backend}.wav"
        subprocess.run([str(ROOT / "build" / "audio_render.exe"), str(out / "opl.log"),
            str(output), "9000000", "63000000", backend], check=True)
        reports[backend] = compare(reference, output)
    (out / "result.json").write_text(json.dumps(reports, indent=2) + "\n")
    print(json.dumps(reports, indent=2))
    return int(not reports["dbopl"]["exact_pcm_equal"])


if __name__ == "__main__":
    raise SystemExit(main())
