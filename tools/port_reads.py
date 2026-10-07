#!/usr/bin/env python3
"""port_reads.py - which I/O ports the game's own code reads, over every route.

    py tools/port_reads.py --data GOG_DIR [--out DIR] [--jobs 6] [--reuse]

Replays each route in tools/routes/ with F117R_INVENTORY set (the machine then
counts every port read and write, and every DOS/BIOS service, and writes them
at exit: counting only, nothing changes) and merges the counts. A difference
between this machine and a reference in a port the game never reads cannot
change what the game does; one in a port it reads is a parity item. The table
printed is the ports read, with counts; machine_diff.py-style comparisons
(tools/fidelity_all.py baselines) are read against it.

--reuse reads the counts of an earlier run in --out instead of replaying.
"""
import argparse
import concurrent.futures
import glob
import os
import re
import subprocess
import sys
from collections import Counter
from pathlib import Path

HERE = Path(__file__).resolve().parent

# Ports where a reference differs from this machine in the machine-behaviour probe
# (tools/fidelity_baseline.json), and what the difference is about.
PROBE_PORTS = {
    0x21: "PIC interrupt mask at start (F8 here, B8 on 86Box)",
    0x3C2: "VGA input status 0 (70 here, 10 on 86Box)",
    0x3C4: "VGA sequencer index",
    0x3C5: "VGA sequencer data (mode 3: memory mode 07 here, 03 on 86Box)",
    0x3C7: "DAC state",
    0x3C8: "DAC write index readback (differs after writes and wrap)",
    0x3C9: "DAC data",
    0x3CE: "VGA graphics controller index",
    0x3CF: "VGA graphics controller data (gc 7 colour don't care)",
    0x330: "MPU-401 data (present here, absent on 86Box)",
    0x331: "MPU-401 status",
    0x40: "PIT counter 0 (speed)",
    0x41: "PIT counter 1",
    0x42: "PIT counter 2",
    0x3DA: "VGA input status 1 (retrace)",
}


def one(route, data, out):
    name = route.stem
    inv = out / (name + ".txt")
    if inv.exists():
        inv.unlink()
    env = dict(os.environ, F117R_INVENTORY=str(inv))
    r = subprocess.run([sys.executable, str(HERE / "run_route.py"), str(route), "--data", str(data),
                        "--engine", "recomp", "--out", str(out / ("run-" + name))],
                       env=env, capture_output=True, text=True)
    (out / (name + ".out")).write_text(r.stdout + r.stderr)
    return name, r.returncode


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--data", required=True)
    ap.add_argument("--out", type=Path, default=Path.home() / "f117-recomp-local" / "port-reads")
    ap.add_argument("--jobs", type=int, default=6)
    ap.add_argument("--reuse", action="store_true")
    a = ap.parse_args()
    a.out.mkdir(parents=True, exist_ok=True)
    routes = sorted(Path(p) for p in glob.glob(str(HERE / "routes" / "*.args")))
    if not a.reuse:
        with concurrent.futures.ThreadPoolExecutor(a.jobs) as ex:
            for name, rc in ex.map(lambda r: one(r, a.data, a.out), routes):
                print("  %-34s rc %s" % (name, rc), flush=True)
    reads, writes, routes_with = Counter(), Counter(), {}
    for f in sorted(a.out.glob("*.txt")):
        for line in f.read_text().splitlines():
            m = re.match(r"port ([0-9A-F]{4}) (in|out) (\d+)", line)
            if not m:
                continue
            port, kind, n = int(m[1], 16), m[2], int(m[3])
            (reads if kind == "in" else writes)[port] += n
            if kind == "in":
                routes_with.setdefault(port, set()).add(f.stem)
    print("\nports read by the game's code (%d routes):" % len(list(a.out.glob("*.txt"))))
    for p in sorted(reads):
        print("  %04X  %10d reads in %2d routes   %s" % (p, reads[p], len(routes_with[p]), PROBE_PORTS.get(p, "")))
    print("\nprobe-difference ports and whether the game reads them:")
    for p, why in sorted(PROBE_PORTS.items()):
        print("  %04X  %-5s %s" % (p, "READ" if reads[p] else "never", why))
    return 0


if __name__ == "__main__":
    sys.exit(main())
