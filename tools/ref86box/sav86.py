#!/usr/bin/env python3
"""sav86.py - a scripted START session on 86Box, and the files it saved.

    py tools/ref86box/sav86.py OUT_DIR --mouse-driver CTMOUSE.EXE [--keys ...] [--mouse ...]
                               [--frames 30000] [--ppm] [--profile D:\\86box\\vmt]

Copies the profile (serial Microsoft mouse on COM1), puts CTMOUSE.EXE
(CuteMouse 2.1, GPL; github.com/davidebreso/ctmouse ships a built ctmouse.exe)
in the image and starts it before F117 in FDAUTO.BAT, runs the traced 86Box
(no window, no sound) with B86_KEYS / B86_MOUSE injected at displayed-frame
counts, and reads ROSTER.FIL back out of the image into OUT_DIR/ROSTER.FIL.
save_parity.py turns a route file into the two schedules (--86box).
"""
import argparse
import os
import re
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
    ap.add_argument("--mouse-driver", required=True)
    ap.add_argument("--keys", default="3000:1:31,3003:0:31,3300:1:03,3303:0:03")
    ap.add_argument("--mouse", default="")
    ap.add_argument("--tail", default="CTMOUSE,F117", help="the commands FDAUTO.BAT ends with, comma separated")
    ap.add_argument("--frames", type=int, default=30000)
    ap.add_argument("--ppm", action="store_true")
    ap.add_argument("--timeout", type=int, default=2400)
    ap.add_argument("--realtime", action="store_true",
                    help="pace 86Box to real time (fast-forward is the default; see build_86box.md)")
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    work = os.path.normpath(os.path.join(a.out, "profile"))
    shutil.rmtree(work, ignore_errors=True)
    os.makedirs(work)
    cfg = open(os.path.join(a.profile, "86box.cfg")).read()
    cfg = re.sub(r"(?m)^mouse_type\s*=.*$", "mouse_type = msserial", cfg)
    open(os.path.join(work, "86box.cfg"), "w").write(cfg)
    shutil.copytree(os.path.join(a.profile, "nvr"), os.path.join(work, "nvr"))
    subprocess.run(["cmd", "/c", "mklink", "/J", os.path.join(work, "roms"), r"D:\86box\app\roms"],
                   stdout=subprocess.DEVNULL, check=True)
    img = os.path.join(work, "f117a.img")
    shutil.copyfile(os.path.join(a.profile, "f117a.img"), img)
    driver = open(a.mouse_driver, "rb").read()

    def install(fs):
        fs.writebytes("/F117A/CTMOUSE.EXE", driver)
        probe86.bare_boot(fs, a.tail.split(","))
    probe86.with_partition(img, install, write=True)
    trace = os.path.join(a.out, "trace")
    env = dict(os.environ, B86_MOUSE=a.mouse)
    if not a.realtime:
        env["B86_FAST"] = "1"
    cmd = ["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", os.path.join(HERE, "trace_86box.ps1"),
           "-Profile", work, "-Out", trace, "-Stop", str(a.frames), "-Keys", a.keys, "-TimeoutSeconds", str(a.timeout)]
    if a.ppm:
        cmd.append("-Ppm")
    subprocess.run(cmd, check=True, env=env)

    def fetch(fs):
        for name in fs.listdir("/F117A"):
            if name.upper() == "ROSTER.FIL":
                return fs.readbytes("/F117A/" + name)
    data = probe86.with_partition(img, fetch)
    if data is None:
        sys.exit("no ROSTER.FIL in the image")
    open(os.path.join(a.out, "ROSTER.FIL"), "wb").write(data)
    print("ROSTER.FIL: %d bytes -> %s" % (len(data), a.out))


if __name__ == "__main__":
    main()
