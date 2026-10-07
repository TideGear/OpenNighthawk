#!/usr/bin/env python3
"""make_profile386.py - the 86Box profile the PC-parity checks use: a 386DX/33.

    py tools/ref86box/make_profile386.py [--src D:\\86box\\vmt] [--out D:\\86box\\vmt386]

The 6 MHz 286 of build_86box.md is far slower than the modelled PC; the game's
timing (scene lengths, music pitch-bend channels) tracks the CPU. This builds a
profile from the 286 one (disk image, sound, video) with the board changed to
`ami495` (OPTi 495SX, AMI BIOS 06/06/92), an i386DX at 33.33 MHz, 1 MB:

  - the BIOS ROM (opt495sx.ami) is fetched from the 86Box roms repository into
    D:\\86box\\app\\roms\\machines\\ami495 when missing;
  - its CMOS is made once by driving the BIOS setup with injected keys (F1 at
    the "CMOS checksum failure" prompt, "Auto configuration with BIOS defaults",
    F10, Y) and keeping the NVR the emulator writes on exit.

Needs the traced 86Box (86box-trace.patch). The resulting profile boots the
disk's FreeDOS in a few seconds of emulated time.
"""
import argparse
import os
import re
import shutil
import subprocess
import sys
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
ROM_URL = "https://raw.githubusercontent.com/86Box/roms/master/machines/ami495/opt495sx.ami"
ROM_DIR = r"D:\86box\app\roms\machines\ami495"
# F1 (setup), Down x3, Enter (BIOS defaults), Y, Enter, any key, F10, Y, Enter; scancodes set 1
SETUP_KEYS = ("700:1:3b,703:0:3b,800:1:50,803:0:50,810:1:50,813:0:50,820:1:50,823:0:50,840:1:1c,843:0:1c,"
              "900:1:15,905:0:15,930:1:1c,935:0:1c,1000:1:1c,1005:0:1c,1060:1:44,1065:0:44,1120:1:15,1125:0:15,"
              "1150:1:1c,1155:0:1c")


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--src", default=r"D:\86box\vmt")
    ap.add_argument("--out", default=r"D:\86box\vmt386")
    ap.add_argument("--scratch", default=r"D:\f117-gate\profile386")
    a = ap.parse_args()
    rom = os.path.join(ROM_DIR, "opt495sx.ami")
    if not os.path.exists(rom):
        os.makedirs(ROM_DIR, exist_ok=True)
        urllib.request.urlretrieve(ROM_URL, rom)
    shutil.rmtree(a.out, ignore_errors=True)
    os.makedirs(os.path.join(a.out, "nvr"))
    cfg = open(os.path.join(a.src, "86box.cfg"), encoding="utf-8-sig").read()
    for key, val in (("cpu_family", "i386dx"), ("cpu_speed", "33333333"), ("machine", "ami495"), ("mem_size", "1024")):
        cfg = re.sub(r"(?m)^%s\s*=.*$" % key, "%s = %s" % (key, val), cfg)
    open(os.path.join(a.out, "86box.cfg"), "w", encoding="utf-8").write(cfg)
    shutil.copyfile(os.path.join(a.src, "f117a.img"), os.path.join(a.out, "f117a.img"))
    # a throwaway run whose only job is to leave a valid NVR
    r = subprocess.run([sys.executable, os.path.join(HERE, "sav86.py"), a.scratch, "--profile", a.out,
                        "--mouse-driver", os.environ.get("CTMOUSE", r"D:\f117-gate\ctm\CTMOUSE.EXE"),
                        "--tail", "F117", "--frames", "1400", "--keys", SETUP_KEYS], capture_output=True, text=True)
    nvr = os.path.join(a.scratch, "profile", "nvr", "ami495.nvr")
    if not os.path.exists(nvr):
        sys.exit("no NVR written\n" + r.stdout + r.stderr)
    shutil.copyfile(nvr, os.path.join(a.out, "nvr", "ami495.nvr"))
    print("profile ->", a.out)


if __name__ == "__main__":
    main()
