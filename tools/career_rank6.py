"""career_rank6.py ENGINE ROSTER98 OUT: END's rank-6 retirement branch, both engines.

Usage: py tools/career_rank6.py recomp|interp SORTIE98_ROSTER.FIL OUT_DIR

Stage the cause of END's rank-6 retirement branch and fly sortie 99.

The roster is sortie 98's actual save from the paired rank-3 chain, with the
selected pilot's rank word (+20h) set to 6 and the total (+32h) to 98 * 285
so the record is one a rank-6 career could hold; nothing else changes. The
99th sortie is flown with the rank-3 photo recording and normal END inputs;
END reads missions (+36h) = 99 and rank = 6 and chooses its remarks itself.
"""
from pathlib import Path
from argparse import Namespace
import re, struct, sys, json, hashlib
repo = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(repo / 'tools'))
import career_check
from machine_api import Machine

engine = sys.argv[1]
src = Path(sys.argv[2])
roster = bytearray(src.read_bytes())
pilot = struct.unpack_from('<H', roster)[0]
base = 2 + pilot * 80
struct.pack_into('<H', roster, base + 0x20, 6)
struct.pack_into('<I', roster, base + 0x32, 98 * 285)
base_out = Path(sys.argv[3]); base_out.mkdir(parents=True, exist_ok=True)
(base_out / 'staged-Roster.Fil').write_bytes(roster)
print('staged', career_check.career(bytes(roster)), hashlib.sha256(roster).hexdigest())
replay_path = repo / 'tools' / 'routes' / 'career_rank6.input'
lines = replay_path.read_text().splitlines()
time_us = int(re.fullmatch(r'# f117r-input ips=9000000 time_us=(\d+)', lines[0])[1])
replay = [l.split() for l in lines[1:] if l and not l.startswith('#')]
out = base_out / f'sortie-99-{engine}'
args = Namespace(data=sys.argv[4] if len(sys.argv) > 4 else 'D:/GOG/F-117A', debrief_route=repo / 'tools/routes/career_promotion.args', steps=17_000_000_000, out=out)
original = Machine.run_until; last = {}
def traced(machine, limit):
    r = original(machine, limit)
    if machine.program.upper() == 'END.EXE':
        slot = max(0, machine.clock - machine.start) // 9_000_000
        if last.get(id(machine)) != slot:
            last[id(machine)] = slot
            machine.screen(out / f'end-{slot:03d}s.ppm')
    return r
Machine.run_until = traced
report, saved = career_check.leg(args, engine, bytes(roster), replay, time_us, out)
print('RANK6', engine, report['errors'], career_check.career(saved), report['hash'])
