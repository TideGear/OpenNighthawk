#!/usr/bin/env python3
"""ROM-free suspend/resume check of the SDL application's wall-clock pacing."""
import argparse
import ctypes as C
import json
import os
from pathlib import Path
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", required=True)
    args = parser.parse_args()
    if os.name != "nt":
        parser.error("controlled process suspension currently requires Windows")
    out = Path(args.out).resolve(); out.mkdir(parents=True, exist_ok=True)
    data = out / "probe"; data.mkdir(exist_ok=True)
    (data / "F117.COM").write_bytes(b"\xeb\xfe")
    kernel = C.WinDLL("kernel32", use_last_error=True)
    kernel.OpenProcess.argtypes = [C.c_uint32, C.c_int, C.c_uint32]
    kernel.OpenProcess.restype = C.c_void_p
    kernel.CloseHandle.argtypes = [C.c_void_p]
    nt = C.WinDLL("ntdll")
    nt.NtSuspendProcess.argtypes = nt.NtResumeProcess.argtypes = [C.c_void_p]
    results = []
    for stall in (0, .8):
        executable = ROOT / "build" / "f117a.exe"
        if not executable.exists(): executable = ROOT / "build" / "Release" / "f117a.exe"
        log = out / f"stall-{stall}.log"
        if log.exists(): log.unlink()
        process = subprocess.Popen([str(executable), "--data", str(data),
            "--save", tempfile.mkdtemp(dir=out), "--engine", "interp", "--no-record",
            "--log", str(log), "--ips", "1000000", "--exit-after", "4000000"],
            env=dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy"), cwd=out)
        try:
            # Exclude SDL/device startup from the paired measurement. A modest
            # clock speed keeps the interpreter below a shared CI runner's CPU
            # budget; this test measures pacing, not execution throughput.
            deadline = time.monotonic() + 15
            while not (log.exists() and "[exec] F117.COM" in log.read_text()):
                if process.poll() is not None: raise RuntimeError("application failed before boot")
                if time.monotonic() > deadline: raise RuntimeError("application boot timed out")
                time.sleep(.01)
            began = time.monotonic()
            if stall:
                time.sleep(1)
                handle = kernel.OpenProcess(0x800, False, process.pid)
                if not handle: raise C.WinError(C.get_last_error())
                try:
                    if nt.NtSuspendProcess(handle): raise RuntimeError("suspend failed")
                    try: time.sleep(stall)
                    finally:
                        if nt.NtResumeProcess(handle): raise RuntimeError("resume failed")
                finally: kernel.CloseHandle(handle)
            process.wait(timeout=15)
            results.append({"stall_seconds": stall, "wall_seconds": time.monotonic() - began,
                            "exit_code": process.returncode})
        finally:
            if process.poll() is None: process.kill(); process.wait()
    added = results[1]["wall_seconds"] - results[0]["wall_seconds"]
    errors = []
    if any(r["exit_code"] for r in results): errors.append("application failed")
    if not .45 < added < 1.2: errors.append("stall changed pacing outside the 100 ms catch-up allowance")
    result = {"runs": results, "added_wall_seconds": added, "errors": errors}
    (out / "result.json").write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(result, indent=2))
    return int(bool(errors))


if __name__ == "__main__":
    raise SystemExit(main())
