"""autorun_hd.py IMG [COMMAND]: make the hard-disk image run COMMAND (default
F117) at the end of FDAUTO.BAT, so a run needs no typed command. The FAT16
partition starts at LBA 17 (build_hdd.py); it is cut out, edited and written back."""
import sys
import tempfile
import warnings
warnings.filterwarnings("ignore")
from pyfatfs.PyFatFS import PyFatFS

img = sys.argv[1]
command = sys.argv[2] if len(sys.argv) > 2 else "F117"
START = 17 * 512
data = bytearray(open(img, "rb").read())
with tempfile.NamedTemporaryFile(suffix=".img", delete=False) as t:
    t.write(data[START:])
    part = t.name
fs = PyFatFS(part)
bat = fs.readtext("/FDAUTO.BAT").replace("\r\n", "\n")
lines = [l for l in bat.split("\n") if l.strip() and l.strip().upper() != command.upper()]
lines.append(command)
fs.writetext("/FDAUTO.BAT", "\r\n".join(lines) + "\r\n")
fs.close()
data[START:] = open(part, "rb").read()
open(img, "wb").write(data)
print("FDAUTO.BAT now ends with", repr(lines[-2:]))
