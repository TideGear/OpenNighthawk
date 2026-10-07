#!/usr/bin/env python3
"""d6_check.py - fix D6 ("stealth mountains") at work in a sector whose cover is 0.

    py tools/d6_check.py --data GOG_DIR [--out DIR] [--route tools/routes/central_europe_airair.front]

Plays a route's front to the start of flight in a theatre that has sectors with
detection cover 0 (Central Europe: 12), reads the cover table VGAME keeps at
DS:B1A0 (one byte per 2,048-unit sector, index (x >> 11) + ((y >> 11) << 4)),
counts the sectors whose cover bits (0x0C) are 0 (12 in Central Europe, as the Reimp
found), then clears the cover bits of the sector the player is in, so the player is in a cover-0
sector. VGAME's detection reads the cover at the player's position, so with fix D6 on the first read there
logs "[fix D6] cover 0 read as 4" and with it off nothing is logged. Prints one
line per run and exits 0 when the fixed run logs the line and the plain one does
not. The route front names the input; no game data is kept.
"""
import argparse
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from machine_api import Machine, RouteInputs  # noqa: E402
from run_route import route_args  # noqa: E402


def run(data, route, fixes, log):
    args = route_args(str(route))
    inputs = []
    i = 0
    while i < len(args):
        if args[i] in ("--type", "--click", "--move", "--hold"):
            inputs += [args[i], args[i + 1]]
        i += 2
    with Machine(data, tempfile.mkdtemp(dir=log.parent), log=log, engine="recomp", fixes=fixes) as m:
        r = RouteInputs(inputs)
        flight = None
        while m.clock < 6_000_000_000:
            r.poll(m)
            m.run_until(m.clock + (100_000 if m.program.upper() in ("F117.COM", "SETUP.EXE", "") else 10_000_000))
            if m.program.upper() == "VGAME.EXE":
                if flight is None:
                    flight = m.clock
                elif m.clock > flight + 400_000_000:
                    break
        ds = (m.psp + 0x10 + 0x1E42) << 4
        cells = [k for k in range(256) if not m.read8(ds + 0xB1A0 + k) & 0x0C]
        if not cells:
            return None, 0
        # Detection runs now and then, not every frame, and the game rewrites the player's position
        # as it flies; the cover table is the world's and stays put. So make the player's own sector
        # a cover-0 one (clear its cover bits) and let the game run until the line shows or time is up.
        px, py = m.read16(ds + 0xC0D0), m.read16(ds + 0xC0DE)
        k = (px >> 11) + ((py >> 11) << 4)
        m.stage_write16(ds + 0xB1A0 + k, m.read16(ds + 0xB1A0 + k) & ~0x0C)
        for _ in range(400):
            m.run_until(m.clock + 5_000_000)
            if "[fix D6]" in log.read_text(errors="replace"):
                break
        return k, len(cells)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--data", required=True)
    ap.add_argument("--route", default=str(HERE / "routes" / "central_europe_airair.front"))
    ap.add_argument("--out", default=None)
    a = ap.parse_args()
    out = Path(a.out) if a.out else Path(tempfile.mkdtemp(prefix="d6-"))
    out.mkdir(parents=True, exist_ok=True)
    ok = True
    for fixes in ((), ("D6",)):
        log = out / ("run-%s.log" % ("fixed" if fixes else "plain"))
        cell, n = run(a.data, a.route, fixes, log)
        hits = sum(1 for l in log.read_text(errors="replace").splitlines() if "[fix D6]" in l)
        print("D6 %-3s: %s cover-0 sectors in the table%s; fix lines in the log: %d" % (
            "on" if fixes else "off", n, "" if cell is None else " (the player's sector %d made one)" % cell, hits))
        ok &= (hits > 0) if fixes else (hits == 0)
        if cell is None:
            ok = False
    print("RESULT:", "fix D6 fires in a cover-0 sector and is silent when off" if ok else "FAILED")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
