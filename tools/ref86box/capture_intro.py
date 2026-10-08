#!/usr/bin/env python3
"""capture_intro.py - the original's intro on 86Box, as a sequence of pictures.

    py tools/ref86box/capture_intro.py OUT_DIR [--seconds 130] [--realtime]
    py tools/ref86box/capture_intro.py OUT_DIR --vnc [--profile D:\\86box\\vmf]

By default the traced 86Box (trace_86box.ps1, the 386 profile, fast-forward
unless --realtime) boots F117 from FDAUTO.BAT, SETUP is answered by key
injection at displayed frames 3000 and 3300 (N, then 2, as sound86.py does),
and the picture on screen is sampled once per emulated second from frame 3300
for --seconds: the same check on emulated time, a function of the machine and
its inputs. --vnc keeps the older real-time capture described below.

The VNC capture starts the headless 86Box (start_vnc86.ps1), waits for DOS, runs F117, answers
SETUP as the routes do (N, then 2), samples the display once a second for
--seconds, and writes OUT_DIR/frames.json: each distinct 320x200 picture with
the first and last time it was seen (seconds after SETUP was answered) and its
hash, plus the PNGs. Mode 13h shows as 640x400 at the top of 86Box's 640x480
frame, so the picture is that region halved; text-mode screens are kept whole.

The time axis is real time on a machine that runs when a client is connected
and is paused otherwise, so it is only as good as the VM's speed: the board
is a 286 (see build_86box.md). The ORDER of pictures does not depend on speed
and is the check that is meaningful today.
"""
import argparse
import csv
import hashlib
import json
import os
import shutil
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import probe86  # noqa: E402
from vnc86 import Rfb, typing, press  # noqa: E402

SETUP_KEYS = "3000:1:31,3003:0:31,3300:1:03,3303:0:03"
SETUP_FRAME = 3300


def traced(a):
    """Sample the traced run's displayed picture once per emulated second after SETUP is answered."""
    from PIL import Image
    work = os.path.normpath(os.path.join(a.out, "profile"))
    shutil.rmtree(work, ignore_errors=True)
    os.makedirs(work)
    shutil.copyfile(os.path.join(a.profile, "86box.cfg"), os.path.join(work, "86box.cfg"))
    shutil.copytree(os.path.join(a.profile, "nvr"), os.path.join(work, "nvr"))
    subprocess.run(["cmd", "/c", "mklink", "/J", os.path.join(work, "roms"), r"D:\86box\app\roms"],
                   stdout=subprocess.DEVNULL, check=True)
    img = os.path.join(work, "f117a.img")
    shutil.copyfile(os.path.join(a.profile, "f117a.img"), img)
    probe86.with_partition(img, lambda fs: probe86.bare_boot(fs, ["F117"]), write=True)
    trace = os.path.join(a.out, "trace")
    shutil.rmtree(trace, ignore_errors=True)
    env = dict(os.environ)
    if not a.realtime:
        env["B86_FAST"] = "1"
    stop = SETUP_FRAME + (a.seconds + 5) * 72          # 70 Hz modes, a few frames spare
    subprocess.run(["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File",
                    os.path.join(HERE, "trace_86box.ps1"), "-Profile", work, "-Out", trace, "-Stop", str(stop),
                    "-Keys", SETUP_KEYS, "-Ppm", "-TimeoutSeconds", "1800"], check=True, env=env)
    rows = [(int(r[0]), int(r[2]), r[5]) for r in csv.reader(open(os.path.join(trace, "frames.csv")))]
    start_us = next(us for n, us, _ in rows if n >= SETUP_FRAME)
    frames, k, shown, prev = [], 0, None, None
    for n, us, h in rows:
        if h != prev:                                   # the patch saves a PPM at each change of picture
            shown, prev = n, h
        while k < a.seconds and us >= start_us + k * 1_000_000:
            pic = Image.open(os.path.join(trace, "f%06d.ppm" % shown)).convert("RGB")
            digest = hashlib.sha256(pic.tobytes()).hexdigest()
            if frames and frames[-1]["hash"] == digest:
                frames[-1]["end"] = float(k)
                frames[-1]["samples"] += 1
            else:
                name = "p%03d.png" % len(frames)
                pic.save(os.path.join(a.out, name))
                frames.append(dict(hash=digest, time=float(k), end=float(k), samples=1, size=list(pic.size), file=name))
            k += 1
    if k < a.seconds:
        sys.exit("the trace ended %d s after SETUP; %d asked for" % (k, a.seconds))
    with open(os.path.join(a.out, "frames.json"), "w") as f:
        json.dump(dict(profile=a.profile, seconds=a.seconds, clock="emulated", frames=frames), f, indent=1)
    for name in os.listdir(trace):
        if name.endswith(".ppm"):
            os.remove(os.path.join(trace, name))
    print("%d distinct pictures in %d emulated s -> %s" % (len(frames), a.seconds, a.out))


def picture(rfb):
    """The frame as 320x200 RGB bytes when it is mode 13h, else the whole frame."""
    from PIL import Image
    img = Image.frombuffer("RGBA", (rfb.w, rfb.h), bytes(rfb.frame), "raw", "BGRA", 0, 1).convert("RGB")
    top = img.crop((0, 0, rfb.w, min(400, rfb.h)))
    rows_below = img.crop((0, 400, rfb.w, rfb.h)).getbbox() if rfb.h > 400 else None
    if rfb.w == 640 and rows_below is None:
        small = top.resize((320, 200), Image.NEAREST)
        return small
    return img


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("out")
    ap.add_argument("--profile", help=r"default D:\86box\vmt386 traced, D:\86box\vmf with --vnc")
    ap.add_argument("--seconds", type=int, default=130)
    ap.add_argument("--boot-wait", type=int, default=55)
    ap.add_argument("--vnc", action="store_true", help="the older real-time capture over VNC")
    ap.add_argument("--realtime", action="store_true", help="traced, but paced to real time")
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    if not a.vnc:
        a.profile = a.profile or r"D:\86box\vmt386"
        return traced(a)
    a.profile = a.profile or r"D:\86box\vmf"
    subprocess.run(["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File",
                    os.path.join(HERE, "start_vnc86.ps1"), "-Profile", a.profile], check=True)
    rfb = None
    for _ in range(60):                           # the port opens when 86Box has started: seconds, longer under load
        time.sleep(2)
        try:
            rfb = Rfb("127.0.0.1", 5900)
            break
        except OSError:
            pass
    if rfb is None:
        sys.exit("86Box did not open its VNC port in 120 s")
    try:
        time.sleep(a.boot_wait)
        typing(rfb, "f117")
        press(rfb, ["Return"])
        for _ in range(60):                       # until SETUP's joystick question is up
            rfb.update(1.0)
            if picture(rfb).size != (320, 200) or True:
                pass
            time.sleep(1)
            if _ >= 8:
                break
        typing(rfb, "n")
        time.sleep(2)
        typing(rfb, "2")
        start = time.time()
        frames, last_hash = [], None
        while time.time() - start < a.seconds:
            rfb.update(1.0)
            img = picture(rfb)
            digest = hashlib.sha256(img.tobytes()).hexdigest()
            now = time.time() - start
            if frames and frames[-1]["hash"] == digest:
                frames[-1]["end"] = now
                frames[-1]["samples"] += 1
            else:
                name = "p%03d.png" % len(frames)
                img.save(os.path.join(a.out, name))
                frames.append(dict(hash=digest, time=now, end=now, samples=1, size=list(img.size), file=name))
            time.sleep(max(0.0, 1.0 - ((time.time() - start) % 1.0)))
        with open(os.path.join(a.out, "frames.json"), "w") as f:
            json.dump(dict(profile=a.profile, seconds=a.seconds, frames=frames), f, indent=1)
        print("%d distinct pictures in %d s -> %s" % (len(frames), a.seconds, a.out))
    finally:
        # this run's 86Box only (traced runs of other profiles may be going beside it)
        subprocess.run(["powershell", "-NoProfile", "-Command",
                        "Get-CimInstance Win32_Process -Filter \"Name='86Box.exe'\" | "
                        "Where-Object { $_.CommandLine -like '*%s*' } | "
                        "ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }" % a.profile])


if __name__ == "__main__":
    main()
