"""Build a 64 MB MBR-partitioned FAT16 disk image: FreeDOS files and the GOG
game, bootable once `fdisk /mbr` and `sys C:` have run (see floppy)."""
import struct, sys, warnings
warnings.filterwarnings("ignore")
from pathlib import Path
from pyfatfs.PyFat import PyFat
from pyfatfs.PyFatFS import PyFatFS

C, H, S = 130, 16, 63                     # 130 * 16 * 63 sectors = 64.0 MB
total = C * H * S
start = S                                 # the partition starts at LBA 63 (cylinder 0, head 1)
size_sectors = total - start
vol = Path("vm/vol.img")
vol.write_bytes(bytes(size_sectors * 512))
pf = PyFat()
pf.mkfs(str(vol), fat_type=PyFat.FAT_TYPE_FAT16, size=size_sectors * 512, sector_size=512,
        number_of_fats=2, label="F117A", volume_id=0x4D50F117, media_type=0xF8)
pf.close()
fs = PyFatFS(str(vol))
fd = PyFatFS("fd13/144m/x86BOOT.img")
def copy_tree(src, srcdir, dst, dstdir):
    for name in src.listdir(srcdir):
        s, d = srcdir.rstrip("/") + "/" + name, dstdir.rstrip("/") + "/" + name
        if src.isdir(s):
            dst.makedirs(d, recreate=True); copy_tree(src, s, dst, d)
        else:
            dst.writebytes(d, src.readbytes(s))
fs.makedirs("/freedos", recreate=True)
copy_tree(fd, "/freedos", fs, "/freedos")
for n in ("fdconfig.sys",):
    fs.writebytes("/" + n, fd.readbytes("/" + n))
fs.writebytes("/KERNEL.SYS", fd.readbytes("/KERNEL.SYS"))
# the boot script: environment only, then the game directory
fs.writetext("/FDAUTO.BAT", fd.readtext("/fdauto.bat").split("alias reboot")[0] + "\r\ncd \F117A\r\n")
game = Path("D:/GOG/F-117A")
fs.makedirs("/F117A", recreate=True)
n = 0
for p in sorted(game.iterdir()):
    if p.is_file():
        fs.writebytes("/F117A/" + p.name.upper(), p.read_bytes()); n += 1
fs.close()
print("game files:", n)
# the MBR: a partition table, boot code to be written by `fdisk /mbr`
mbr = bytearray(512)
def chs(lba):
    c, rem = divmod(lba, H * S); h, s = divmod(rem, S)
    return bytes([h, ((c >> 2) & 0xC0) | (s + 1), c & 0xFF])
mbr[446:462] = bytes([0x80]) + chs(start) + bytes([0x06]) + chs(total - 1) + struct.pack("<II", start, size_sectors)
mbr[510:512] = b"\x55\xAA"
img = Path("vm/f117a.img")
with img.open("wb") as f:
    f.write(mbr); f.write(b"\0" * 512 * (start - 1)); f.write(vol.read_bytes())
print(img, img.stat().st_size)
