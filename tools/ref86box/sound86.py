#!/usr/bin/env python3
"""sound86.py - the intro's AdLib register writes on 86Box, in emulated time.

    py tools/ref86box/sound86.py OUT_DIR [--profile D:\\86box\\vmt] [--frames 12000]
                                 [--keys "3000:1:31,3003:0:31,3300:1:03,3303:0:03"]

Runs the traced 86Box (trace_86box.ps1, no window, no sound) with B86_OPL set
and F117 started from FDAUTO.BAT, SETUP answered by key injection at displayed-frame
counts (N, then 2), and leaves OUT_DIR/opl86.log: "microseconds register value"
for every write to the AdLib's ports, microseconds of the emulated 286's clock.
sound_compare86.py compares it with this machine's log.
"""
import argparse
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import probe86  # noqa: E402


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("out")
    ap.add_argument("--profile", default=r"D:\86box\vmt386")
    ap.add_argument("--frames", type=int, default=12000)
    ap.add_argument("--keys", default="3000:1:31,3003:0:31,3300:1:03,3303:0:03")
    ap.add_argument("--timeout", type=int, default=1200)
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    work = os.path.normpath(os.path.join(a.out, "profile"))
    shutil.rmtree(work, ignore_errors=True)
    os.makedirs(work)
    shutil.copyfile(os.path.join(a.profile, "86box.cfg"), os.path.join(work, "86box.cfg"))
    shutil.copytree(os.path.join(a.profile, "nvr"), os.path.join(work, "nvr"))
    subprocess.run(["cmd", "/c", "mklink", "/J", os.path.join(work, "roms"), r"D:\86box\app\roms"],
                   stdout=subprocess.DEVNULL, check=True)
    img = os.path.join(work, "f117a.img")
    shutil.copyfile(os.path.join(a.profile, "f117a.img"), img)

    def install(fs):
        probe86.bare_boot(fs, ["F117"])
    probe86.with_partition(img, install, write=True)
    trace = os.path.join(a.out, "trace")
    opl = os.path.join(a.out, "opl86.log")
    env = dict(os.environ, B86_OPL=opl)
    subprocess.run(["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File",
                    os.path.join(HERE, "trace_86box.ps1"), "-Profile", work, "-Out", trace,
                    "-Stop", str(a.frames), "-Keys", a.keys, "-TimeoutSeconds", str(a.timeout)], check=True, env=env)
    n = sum(1 for _ in open(opl)) if os.path.exists(opl) else 0
    print("%d AdLib writes -> %s" % (n, opl))
    return 0 if n else 1


if __name__ == "__main__":
    sys.exit(main())
