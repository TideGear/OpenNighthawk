#!/usr/bin/env python3
"""capture_intro.py - the original's intro on 86Box, as a sequence of pictures.

    py tools/ref86box/capture_intro.py OUT_DIR [--profile D:\\86box\\vmf] [--seconds 130]

Starts the headless 86Box (start_vnc86.ps1), waits for DOS, runs F117, answers
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
import hashlib
import json
import os
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from vnc86 import Rfb, typing, press  # noqa: E402


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
    ap.add_argument("--profile", default=r"D:\86box\vmf")
    ap.add_argument("--seconds", type=int, default=130)
    ap.add_argument("--boot-wait", type=int, default=55)
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    subprocess.run(["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File",
                    os.path.join(HERE, "start_vnc86.ps1"), "-Profile", a.profile], check=True)
    time.sleep(6)
    rfb = Rfb("127.0.0.1", 5900)
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
        subprocess.run(["powershell", "-NoProfile", "-Command",
                        "Get-Process 86Box -ErrorAction SilentlyContinue | Stop-Process -Force"])


if __name__ == "__main__":
    main()
