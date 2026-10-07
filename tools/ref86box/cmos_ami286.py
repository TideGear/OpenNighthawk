"""cmos_ami286.py NVR: write a CMOS image the AMI 286 BIOS (86Box `ami286`)
accepts with no keypress.

POST stops at an F1 prompt (drawn below the rows a window capture shows, so
the screen looks black) whenever register 0Eh reports a problem. The cause
found on 6 Oct 2026: the extended-memory size. The VM has 640 KB and no
extended memory, so the CMOS must say 0 at 17h/18h and 30h/31h; claiming
384 KB set 0Eh bit 4 (memory size miscompare). Also needed: a valid RTC
date, floppy A as type 2 (1.2 MB 5.25"), an equipment byte for one floppy
and VGA, and the standard checksum of 10h-2Dh at 2Eh/2Fh. A boot sector
that sets mode 3 and prints a line (hello_floppy.py) then runs.
"""
import sys

cm = bytearray(128)
for reg, value in {0x00: 0x00, 0x02: 0x00, 0x04: 0x12, 0x06: 0x02, 0x07: 0x07, 0x08: 0x10, 0x09: 0x91}.items():
    cm[reg] = value                      # 12:00:00, Monday 7 Oct 1991 (BCD)
cm[0x0A], cm[0x0B], cm[0x0D], cm[0x0E] = 0x26, 0x02, 0x80, 0x00
cm[0x10] = 0x20                          # A: 1.2 MB, B: none
cm[0x12] = 0x00                          # no CMOS hard disk (the XT-IDE ROM has its own)
cm[0x14] = 0x01                          # one floppy, EGA/VGA, no FPU
cm[0x15], cm[0x16] = 0x80, 0x02          # 640 KB base memory
cm[0x32] = 0x19                          # century
s = sum(cm[0x10:0x2E])
cm[0x2E], cm[0x2F] = (s >> 8) & 0xFF, s & 0xFF
open(sys.argv[1], "wb").write(cm)
print(cm[:0x34].hex(" "))
