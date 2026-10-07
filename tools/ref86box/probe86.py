#!/usr/bin/env python3
"""probe86.py - run tools/fidelity.py's probe program inside 86Box and return its answers.

    py tools/ref86box/probe86.py OUT_DIR [--profile D:\\86box\\vmt] [--frames 9000]

The probe (F117.COM) and its EXEC'd child (CHILD.EXE) are written into a copy
of the 86Box profile's disk image, replacing the game's F117.COM there, and the
image's FDAUTO.BAT runs F117. 86Box (tools/ref86box/trace_86box.ps1, no window,
no sound) runs for --frames displayed frames, then the answer files OUT.BIN and
CHILD.BIN are read back out of the image. The probe measures this machine, so
fields that depend on CPU speed differ from a 9 MIPS PC by design; the
comparison (fidelity_all.py) treats those separately.
"""
import argparse
import os
import shutil
import subprocess
import sys
import tempfile
import warnings

warnings.filterwarnings("ignore")
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
import fidelity  # noqa: E402
from pyfatfs.PyFatFS import PyFatFS  # noqa: E402

PART = 17 * 512                      # the FAT16 partition starts at LBA 17 (build_hdd.py)


def with_partition(image, fn, write=False):
    data = bytearray(open(image, "rb").read())
    with tempfile.NamedTemporaryFile(suffix=".img", delete=False) as t:
        t.write(data[PART:])
        part = t.name
    fs = PyFatFS(part)
    try:
        result = fn(fs)
    finally:
        fs.close()
    if write:
        data[PART:] = open(part, "rb").read()
        open(image, "wb").write(data)
    os.unlink(part)
    return result


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("out")
    ap.add_argument("--profile", default=r"D:\86box\vmt")
    ap.add_argument("--frames", type=int, default=9000)
    ap.add_argument("--timeout", type=int, default=900)
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    probe, _ = fidelity.build_probe()
    child, _ = fidelity.build_child()
    image = os.path.join(a.profile, "f117a.img")
    work = os.path.normpath(os.path.join(a.out, "profile"))
    shutil.rmtree(work, ignore_errors=True)
    os.makedirs(work)
    for name in ("86box.cfg",):
        shutil.copyfile(os.path.join(a.profile, name), os.path.join(work, name))
    shutil.copytree(os.path.join(a.profile, "nvr"), os.path.join(work, "nvr"))
    subprocess.run(["cmd", "/c", "mklink", "/J", os.path.join(work, "roms"), r"D:\86box\app\roms"],
                   stdout=subprocess.DEVNULL, check=True)
    img = os.path.join(work, "f117a.img")
    shutil.copyfile(image, img)

    def install(fs):
        fs.writebytes("/F117A/F117.COM", probe)
        fs.writebytes("/F117A/CHILD.EXE", child)
        for gone in ("/F117A/OUT.BIN", "/F117A/CHILD.BIN"):
            if fs.exists(gone):
                fs.remove(gone)
        bat = fs.readtext("/FDAUTO.BAT").replace("\r\n", "\n")
        lines = [l for l in bat.split("\n") if l.strip() and l.strip().upper() != "F117"] + ["F117"]
        fs.writetext("/FDAUTO.BAT", "\r\n".join(lines) + "\r\n")
    with_partition(img, install, write=True)
    trace = os.path.join(a.out, "trace")
    subprocess.run(["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File",
                    os.path.join(HERE, "trace_86box.ps1"), "-Profile", work, "-Out", trace,
                    "-Stop", str(a.frames), "-TimeoutSeconds", str(a.timeout)], check=True)

    def fetch(fs):
        return [fs.readbytes("/F117A/" + n) if fs.exists("/F117A/" + n) else None for n in ("OUT.BIN", "CHILD.BIN")]
    out, child_out = with_partition(img, fetch)
    if not out or not child_out:
        sys.exit("the probe wrote no answers in %d frames (see %s)" % (a.frames, trace))
    open(os.path.join(a.out, "86box.bin"), "wb").write(out)
    open(os.path.join(a.out, "86box-child.bin"), "wb").write(child_out)
    print("probe answers: %d + %d bytes -> %s" % (len(out), len(child_out), a.out))


if __name__ == "__main__":
    main()
