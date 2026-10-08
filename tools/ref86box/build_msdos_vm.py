#!/usr/bin/env python3
"""build_msdos_vm.py - an 86Box 386DX/33 profile booting MS-DOS (4.01, 5.0, 6.22), for the 386 timing profile's reference.

    py tools/ref86box/build_msdos_vm.py --disks DIR --out D:\\86box\\vmt386dosNNN [--src D:\\86box\\vmt386]
                                        [--data D:\\GOG\\F-117A]

DIR holds the owner's MS-DOS setup disks (images); the one with IO.SYS, MSDOS.SYS and COMMAND.COM is
the system disk, and its COMMAND.COM names the version (DOS-VERSION.txt in the profile). The profile
is vmt386's board (make_profile386.py: ami495, i386DX at 33.33 MHz, 1 MB, IBM VGA, AdLib, XT-IDE)
with a new 62 MB hard disk:

  1. an empty FAT16 partition (build_hdd.py's geometry, patch_hd_boot.py's BPB, the FreeDOS VM's MBR
     code);
  2. what `SYS C:` does: that MS-DOS's boot code (the system disk's boot sector, the partition's own BPB
     kept) and IO.SYS, MSDOS.SYS (system, hidden, read-only) and COMMAND.COM written first, so the
     first two root entries and the first clusters are the system files, as its boot code needs.
     DRVSPACE.BIN is left out, so DOS loads no compression driver;
  3. a first boot in 86Box, which shows the disk boots (AUTOEXEC.BAT writes a file); then the game (a
     copy of the install) goes into C:\\F117A and CONFIG.SYS / AUTOEXEC.BAT give a plain period
     setup: FILES=20, BUFFERS=20, no disk cache, then the game. The board's BIOS reports no extended
     memory, so there is no HIMEM or DOS=HIGH.

MS-DOS is the owner's copy: the disks and the built image stay outside the repository.
"""
import argparse
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import warnings

warnings.filterwarnings("ignore")
HERE = os.path.dirname(os.path.abspath(__file__))
from pyfatfs.PyFat import PyFat          # noqa: E402
from pyfatfs.PyFatFS import PyFatFS      # noqa: E402

C, H, S = 940, 8, 17                     # build_hdd.py: IBM AT drive type 4
TOTAL = C * H * S
START = S                                # the partition at LBA 17

CONFIG = "FILES=20\r\nBUFFERS=20\r\n"     # the board reports no extended memory: no HIMEM, no DOS=HIGH
AUTOEXEC = "@ECHO OFF\r\nPROMPT $P$G\r\nPATH C:\\DOS\r\nCD \\F117A\r\n"


def partition_image(path, mbr_from):
    """build_hdd.py's empty FAT16 partition and BPB geometry (patch_hd_boot.py), with the boot code of
    `mbr_from`'s MBR (the FreeDOS VM's, written by its `fdisk /mbr`), which loads the active
    partition's boot sector from the partition table with the BIOS's drive number."""
    size = TOTAL - START
    with tempfile.NamedTemporaryFile(suffix=".img", delete=False) as t:
        t.write(bytes(size * 512))
        vol = t.name
    pf = PyFat()
    pf.mkfs(vol, fat_type=PyFat.FAT_TYPE_FAT16, size=size * 512, sector_size=512, number_of_fats=2,
            label="F117A", volume_id=0x4D50F117, media_type=0xF8)
    pf.close()
    body = bytearray(open(vol, "rb").read())
    os.unlink(vol)
    struct.pack_into("<HHI", body, 24, S, H, START)        # sectors/track, heads, hidden sectors

    def chs(lba):
        c, rem = divmod(lba, H * S)
        h, s = divmod(rem, S)
        return bytes([h, ((c >> 2) & 0xC0) | (s + 1), c & 0xFF])
    mbr = bytearray(512)
    mbr[:446] = open(mbr_from, "rb").read(446)
    mbr[446:462] = bytes([0x80]) + chs(START) + bytes([0x06]) + chs(TOTAL - 1) + struct.pack("<II", START, size)
    mbr[510:512] = b"\x55\xAA"
    with open(path, "wb") as f:
        f.write(mbr)
        f.write(b"\0" * 512 * (START - 1))
        f.write(body)


def with_partition(image, fn, write=False):
    data = bytearray(open(image, "rb").read())
    with tempfile.NamedTemporaryFile(suffix=".img", delete=False) as t:
        t.write(data[START * 512:])
        part = t.name
    fs = PyFatFS(part)
    try:
        r = fn(fs)
    finally:
        fs.close()
    if write:
        data[START * 512:] = open(part, "rb").read()
        open(image, "wb").write(data)
    os.unlink(part)
    return r


def install_system(image, disk1):
    """`SYS C:` on the empty partition: the system files first, then MS-DOS's boot code."""
    floppy = PyFatFS(disk1, read_only=True)
    files = [(n, floppy.readbytes("/" + n)) for n in ("IO.SYS", "MSDOS.SYS", "COMMAND.COM")]
    floppy.close()
    data = bytearray(open(image, "rb").read())
    part = START * 512
    res = struct.unpack_from("<H", data, part + 14)[0]
    nf = data[part + 16]
    spf = struct.unpack_from("<H", data, part + 22)[0]
    root = part + (res + nf * spf) * 512
    data[root:root + 32] = bytes(32)                    # the formatter's volume label: the entry goes to IO.SYS
    open(image, "wb").write(data)

    def write(fs):
        for n, b in files:
            fs.writebytes("/" + n, b)
    with_partition(image, write, write=True)
    data = bytearray(open(image, "rb").read())
    for i, (n, b) in enumerate(files[:2]):
        e = root + 32 * i
        want = (n.split(".")[0].ljust(8) + n.split(".")[1]).encode()
        if bytes(data[e:e + 11]) != want:
            sys.exit("root entry %d is %r, not %s" % (i, bytes(data[e:e + 11]), n))
        data[e + 11] = 0x07                             # read-only, hidden, system
    # IO.SYS must start the data area and be contiguous for its first sectors
    fat = part + res * 512
    first = struct.unpack_from("<H", data, root + 26)[0]
    chain, cl = [], first
    while cl < 0xFFF8 and len(chain) < 64:
        chain.append(cl)
        cl = struct.unpack_from("<H", data, fat + 2 * cl)[0]
    if first != 2 or chain != list(range(2, 2 + len(chain))):
        sys.exit("IO.SYS is not contiguous from cluster 2: %s" % chain[:8])
    boot = open(disk1, "rb").read(512)
    data[part:part + 11] = boot[:11]                    # jump and OEM name (MSDOS5.0)
    data[part + 0x3E:part + 0x200] = boot[0x3E:0x200]   # the boot code; our BPB and drive 80h stay
    open(image, "wb").write(data)


def system_disk(disks):
    """The image holding IO.SYS, MSDOS.SYS and COMMAND.COM, and the version its COMMAND.COM reports."""
    for name in sorted(os.listdir(disks)):
        if not name.lower().endswith(".img"):
            continue
        fs = PyFatFS(os.path.join(disks, name), read_only=True)
        have = {n.upper() for n in fs.listdir("/")}
        if {"IO.SYS", "MSDOS.SYS", "COMMAND.COM"} <= have:
            versions = re.findall(rb"Version ([0-9]\.[0-9]{2})", fs.readbytes("/COMMAND.COM"))
            fs.close()
            return os.path.join(disks, name), max(v.decode() for v in versions) if versions else "?"
        fs.close()
    sys.exit("no image in %s holds IO.SYS, MSDOS.SYS and COMMAND.COM" % disks)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--disks", required=True)
    ap.add_argument("--src", default=r"D:\86box\vmt386")
    ap.add_argument("--out", required=True)
    ap.add_argument("--data", default=r"D:\GOG\F-117A")
    ap.add_argument("--frames", type=int, default=1500, help="frames for the first boot")
    a = ap.parse_args()
    a.out = os.path.normpath(a.out)          # mklink needs backslashes
    root =os.path.normcase(os.path.abspath(os.path.join(HERE, "..", "..")))
    if os.path.normcase(os.path.abspath(a.out)).startswith(root):
        sys.exit("the profile holds MS-DOS: build it outside the repository")
    os.makedirs(a.out, exist_ok=True)
    img = os.path.join(a.out, "f117a.img")
    partition_image(img, os.path.join(a.src, "f117a.img"))
    boot_disk, version = system_disk(a.disks)
    install_system(img, boot_disk)
    sysfiles = with_partition(img, lambda fs: sorted(n.upper() for n in fs.listdir("/")))
    shutil.copytree(os.path.join(a.src, "nvr"), os.path.join(a.out, "nvr"), dirs_exist_ok=True)
    shutil.copyfile(os.path.join(a.src, "86box.cfg"), os.path.join(a.out, "86box.cfg"))
    roms = os.path.join(a.out, "roms")
    if not os.path.exists(roms):
        subprocess.run(["cmd", "/c", "mklink", "/J", roms, r"D:\86box\app\roms"], stdout=subprocess.DEVNULL, check=True)

    # 3. a first boot: MS-DOS writes a file, showing the disk boots and its files system is sound
    with_partition(img, lambda fs: fs.writetext("/AUTOEXEC.BAT", "ECHO DONE > C:\\BOOTED.TXT\r\n"), write=True)
    trace = os.path.join(a.out, "first-boot")
    subprocess.run(["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File",
                    os.path.join(HERE, "trace_86box.ps1"), "-Profile", a.out, "-Out", trace,
                    "-Stop", str(a.frames), "-TimeoutSeconds", "600"], check=True)
    if not with_partition(img, lambda fs: fs.exists("/BOOTED.TXT")):
        sys.exit("the first boot did not reach AUTOEXEC.BAT: see %s" % trace)

    def install(fs):
        fs.remove("/BOOTED.TXT")
        fs.makedirs("/F117A", recreate=True)
        n = 0
        for name in sorted(os.listdir(a.data)):
            p = os.path.join(a.data, name)
            if os.path.isfile(p) and not name.lower().startswith(("unins", "goggame", "gog", "launch", "support")):
                fs.writebytes("/F117A/" + name.upper(), open(p, "rb").read())
                n += 1
        fs.writetext("/CONFIG.SYS", CONFIG)
        fs.writetext("/AUTOEXEC.BAT", AUTOEXEC + "F117\r\n")
        return n
    n = with_partition(img, install, write=True)
    open(os.path.join(a.out, "DOS-VERSION.txt"), "w").write("MS-DOS %s from %s\n" % (version, os.path.basename(boot_disk)))
    print("profile %s: MS-DOS %s (%s), %d game files" % (a.out, version, ", ".join(sysfiles), n))


if __name__ == "__main__":
    main()
