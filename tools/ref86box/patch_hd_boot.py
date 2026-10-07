"""patch_hd_boot.py IMG: make build_hdd.py's image bootable on 86Box's ami286.

The MBR build_hdd.py writes has a partition table and no boot code, and the
FAT boot sector's BPB has zero sectors/track and heads (FreeDOS then fails
"Error reading from drive C"). This writes a tiny MBR that copies itself to
0000:0600 and chain-loads the active partition's boot sector (CHS 0/1/1, DL
from the BIOS) and sets the BPB geometry (17 x 8, 17 hidden sectors).
Result on 6 Oct 2026: POST, XT-IDE, FreeDOS, `C:\F117A>`.
"""
import struct
import sys

img = bytearray(open(sys.argv[1], "rb").read())
code = bytes.fromhex("FA31C08ED0BC007C8ED88EC0FB" "BE007CBF0006B90001FCF3A5" "EA1E060000"
                     "B80102BB007CB90100" "31D2CD13" "7205EA007C0000" "31C0CD13EBE6")
img[:len(code)] = code
img[510:512] = b"\x55\xAA"
struct.pack_into("<HHI", img, 17 * 512 + 24, 17, 8, 17)
open(sys.argv[1], "wb").write(img)
