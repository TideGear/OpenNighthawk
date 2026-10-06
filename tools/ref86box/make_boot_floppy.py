"""make_boot_floppy.py: a copy of the FreeDOS 1.3 boot floppy whose startup
script installs the boot code and system onto the hard disk image that
build_hdd.py makes, unattended: `fdisk /mbr`, then `sys c:`, then a marker file
C:\SYSDONE.TXT. Run in D:/86box (needs `pip install pyfatfs`); writes vm/boot.img."""
import shutil
import warnings
warnings.filterwarnings("ignore")
from pyfatfs.PyFatFS import PyFatFS

B = chr(92)
shutil.copy("fd13/120m/x86BOOT.img", "vm/boot.img")
fs = PyFatFS("vm/boot.img")
fs.writetext("/fdauto.bat", "@echo off\r\nset PATH=" + B + "freedos" + B + "bin\r\nfdisk /mbr\r\nsys c:\r\n"
                            "echo SYSDONE > c:" + B + "sysdone.txt\r\n")
fs.close()
print("vm/boot.img")
