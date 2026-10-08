#!/usr/bin/env python3
"""The closed-loop supply drop of cargo_pilot.py, flown in the DOSBox-X reference instead of the Machine.

The front end (SETUP, START's mission choice and the hangar) is the recorded input of the route, given
to DOSBox-X as raw scancodes and driver positions in emulated time. From VGAME's start the emulator
pauses every 200 ms, dumps the data segment bytes the pilot reads (tools/routes/cargo_pilot.reads), and
waits for the keys and clicks the pilot queues for that tick. The pilot, its controls and its verdict
are cargo_pilot.py's own; this file only supplies the machine.

    py tools/dosbox_cargo_pilot.py --data GOG_DIR --front tools/routes/cargo_pilot.input --out DIR
    py tools/dosbox_cargo_pilot.py --data GOG_DIR --front tools/routes/cargo_pilot.input --out DIR --trace-reads FILE
"""
import argparse
import csv
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import time

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

from machine_api import Machine  # noqa: E402
from cargo_pilot import pilot_state, control  # noqa: E402
from cargo_check import errors, impacts  # noqa: E402

DOSBOX = "D:/86box-src/dbx-src/src/dosbox-x.exe"
READS = HERE / "routes" / "cargo_pilot.reads"
MACHINE_PSP = 6506
DS_BASE = ((MACHINE_PSP + 0x10 + 0x1E42) << 4)
MEM_MASK = 0xFFFFF
IPS = 9_000_000
FRONT_END_CLOCK = 3371769171
TICK_MS = 200
SETUP_CLOCK = 90000


def key_bytes(text):
    # keys.c's keys_next: one (make, break) pair per key, with the same escapes and modifiers.
    esc = {"r": 0x1C, "s": 0x39, "e": 0x01, "t": 0x0F, "b": 0x0E, "U": 0x48, "D": 0x50, "L": 0x4B,
           "R": 0x4D, "0": 0x44, "\\": 0x2B}
    grey_keys = "UDLR"
    rows = ["1234567890-=", "!@#$%^&*()_+", "qwertyuiop[]", "QWERTYUIOP{}", "asdfghjkl;'`",
            "ASDFGHJKL:\"~", "zxcvbnm,./", "ZXCVBNM<>?"]
    codes = [0x02, 0x02, 0x10, 0x10, 0x1E, 0x1E, 0x2C, 0x2C]
    shifted = [False, True, False, True, False, True, False, True]

    def scancode(ch):
        for row, base, sh in zip(rows, codes, shifted):
            if ch in row:
                return base + row.index(ch), sh
        special = {"\\": (0x2B, False), "|": (0x2B, True), " ": (0x39, False), "\r": (0x1C, False),
                   "\x1b": (0x01, False), "\t": (0x0F, False), "\b": (0x0E, False)}
        return special.get(ch, (0, False))

    out, pos = [], 0
    while pos < len(text):
        alt = ctrl = grey = shift = False
        code = 0
        if text[pos] == "\\" and pos + 2 < len(text) and text[pos + 1] in "ac":
            alt, ctrl = text[pos + 1] == "a", text[pos + 1] == "c"
            pos += 2
            code, _ = scancode(text[pos])
        elif text[pos] == "\\" and pos + 1 < len(text):
            pos += 1
            ch = text[pos]
            if ch in esc:
                code, grey = esc[ch], ch in grey_keys
            elif "1" <= ch <= "9":
                code = 0x3B + ord(ch) - ord("1")
        else:
            code, shift = scancode(text[pos])
        pos += 1
        if not code:
            continue
        make, brk = [], []
        if shift: make.append(0x2A)
        if alt: make.append(0x38)
        if ctrl: make.append(0x1D)
        if grey: make.append(0xE0)
        make.append(code)
        if grey: brk.append(0xE0)
        brk.append(code | 0x80)
        if ctrl: brk.append(0x9D)
        if alt: brk.append(0xB8)
        if shift: brk.append(0xAA)
        out.append((make, brk))
    return out


def front_spec(route_lines):
    # The recorded front, as scheduled from SETUP's start; the same timing as machine_api's replay.
    items = []
    for line in route_lines:
        if not line or line.startswith("#"):
            continue
        p = line.split()
        clock = int(p[1])
        if clock >= FRONT_END_CLOCK:
            break
        ms = (clock - SETUP_CLOCK) * 1000 / IPS
        if p[0] == "K":
            items.append("%.4f|r|%s" % (ms, p[2]))
        else:
            x, y, buttons = int(p[2]), int(p[3]), int(p[4])
            items.append("%.4f|d|%d,%d" % (ms, 2 * x, y))
            items.append("%.4f|b|%d" % (ms, buttons))
    return ";".join(items)


def read_ranges():
    ranges = []
    for line in READS.read_text().splitlines():
        if line.strip() and not line.startswith("#"):
            off, length = line.split()
            ranges.append((int(off, 0), int(length, 0)))
    return ranges


class DosboxMachine:
    """The subset of machine_api.Machine that cargo_pilot's controls and observers use."""

    SLICE = Machine.SLICE
    EXITED = Machine.EXITED
    ips = IPS
    psp = MACHINE_PSP

    def __init__(self, data, front_route, out, time_us):
        self.out = out
        self.state_path = out / "loop.state"
        self.reply_prefix = str(out / "reply.")
        self.ranges = read_ranges()
        conf = out / "loop.conf"
        conf.write_text("\n".join([
            "[sdl]", "fullscreen=false", "output=surface", "[mixer]", "nosound=true",
            "[autoexec]", "@echo off", 'mount C "%s"' % data, "c:", "keyb us", "cls",
            "autotype -w 5 -p 0.8 n 2", "f117", "exit", ""]))
        env = dict(os.environ, SDL_VIDEODRIVER="dummy",
                   DBX_AUTO_INPUT_AT="SETUP", DBX_AUTO_INPUT=front_spec(front_route),
                   DBX_WALL_US=str(time_us + SETUP_CLOCK * 1_000_000 // IPS),
                   DBX_LOOP_AT="VGAME", DBX_LOOP_STATE=str(self.state_path),
                   DBX_LOOP_REPLY=self.reply_prefix, DBX_LOOP_EVERY=str(TICK_MS),
                   DBX_LOOP_READS=",".join("0x%x:%d" % r for r in self.ranges),
                   DBX_AUTO_LOG=str(out / "auto.log"))
        self.proc = subprocess.Popen(
            [DOSBOX, "-silent", "-nogui", "-conf", str(Path(data) / "dosboxF117A.conf"), "-conf", str(conf),
             "-time-limit", "36000"], env=env, cwd=os.path.dirname(DOSBOX), stdout=subprocess.DEVNULL,
            stderr=open(out / "dosbox.stderr", "w"))
        self.dump = {}
        self.offset = 0
        self.buffer = b""
        self.seq = 0
        self.pending = []
        self.clock = None
        self.base = None
        self.program = "VGAME.EXE"
        self.hash = None
        self.start = None
        self._take_state()
        self.start = self.clock

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()

    def _new_lines(self):
        if self.state_path.exists():
            with open(self.state_path, "rb") as f:
                f.seek(self.offset)
                self.buffer += f.read()
        end = self.buffer.rfind(b"\n")
        if end < 0:
            return []
        complete, self.buffer = self.buffer[:end + 1], self.buffer[end + 1:]
        self.offset += len(complete)
        return complete.decode().splitlines()

    def _take_state(self):
        deadline = time.time() + 7200
        while True:
            lines = self._new_lines()
            if lines:
                break
            if self.proc.poll() is not None:
                raise RuntimeError("DOSBox-X exited before the next tick")
            if time.time() > deadline:
                raise TimeoutError("no DOSBox-X tick")
            time.sleep(0.001)
        line = lines[0]
        parts = line.split()
        self.seq = int(parts[0])
        ms = float(parts[1])
        if self.base is None:
            self.base = FRONT_END_CLOCK - round(ms * IPS / 1000)
        self.clock = round(ms * IPS / 1000) + self.base
        data = [int(b, 16) for b in parts[6:]]
        expect = sum(n for _, n in self.ranges)
        if len(data) != expect:
            raise RuntimeError("state line has %d bytes, read set has %d" % (len(data), expect))
        self.dump = {}
        at = 0
        for off, n in self.ranges:
            for i in range(n):
                self.dump[(off + i) & MEM_MASK] = data[at]
                at += 1

    def read8(self, address):
        off = (address - DS_BASE) & MEM_MASK
        if off not in self.dump:
            raise KeyError("read at DS+0x%x is outside the DOSBox-X read set" % off)
        return self.dump[off]

    def read16(self, address):
        return self.read8(address) | (self.read8(address + 1) << 8)

    def type(self, at, keys, *, hold_ms=60, gap_ms=60):
        hold, gap = IPS * hold_ms // 1000, IPS * gap_ms // 1000
        for make, brk in key_bytes(keys):
            for code in make:
                self._queue(at, code)
            for code in brk:
                self._queue(at + hold, code)
            at += hold + gap

    def _queue(self, at, code):
        ms = max(0.0, (at - self.clock) * 1000 / IPS)
        self.pending.append("%.4f|r|%02x" % (ms, code))

    def run_until(self, until):
        self._reply("%s" % ";".join(self.pending))
        self.pending = []
        self._take_state()
        return self.SLICE

    def _reply(self, spec):
        final = "%s%d" % (self.reply_prefix, self.seq)
        with open(final + ".tmp", "w") as f:
            f.write(spec + "\n")
        os.replace(final + ".tmp", final)
        previous = "%s%d" % (self.reply_prefix, self.seq - 1)
        if os.path.exists(previous):
            os.remove(previous)

    def screen(self, path):
        return None

    def close(self):
        if self.proc.poll() is None:
            self._reply("stop")
            try:
                self.proc.wait(timeout=30)
            except subprocess.TimeoutExpired:
                self.proc.kill()


def trace_reads(data, front, out, target):
    seen = set()
    orig8, orig16 = Machine.read8, Machine.read16

    def read8(self, address):
        seen.add((address, 1))
        return orig8(self, address)

    def read16(self, address):
        seen.add((address, 2))
        return orig16(self, address)

    Machine.read8, Machine.read16 = read8, read16
    sys.argv = ["cargo_pilot.py", "--data", data, "--front", str(front), "--out", str(out)]
    from cargo_pilot import main as pilot_main
    try:
        pilot_main()
    finally:
        Machine.read8, Machine.read16 = orig8, orig16
        offsets = set()
        for address, n in seen:
            for i in range(n):
                offsets.add((address + i - DS_BASE) & MEM_MASK)
        ranges, run = [], []
        for off in sorted(offsets):
            if run and off == run[-1] + 1:
                run.append(off)
            else:
                if run:
                    ranges.append((run[0], len(run)))
                run = [off]
        if run:
            ranges.append((run[0], len(run)))
        target.write_text("# DS-relative bytes read by cargo_pilot on the Machine: offset length\n" +
                          "".join("0x%x %d\n" % r for r in ranges))
        print("read set", len(ranges), "ranges,", sum(n for _, n in ranges), "bytes", flush=True)


def fly(args):
    route = args.front.read_text().splitlines()
    header = re.fullmatch(r"# f117r-input ips=9000000 time_us=(\d+)", route[0] if route else "")
    if not header:
        raise ValueError("the front file needs its recorded start clock header")
    args.out.mkdir(parents=True, exist_ok=False)
    rows, tick = [], 0
    with DosboxMachine(args.data, route, args.out, int(header[1])) as machine:
        start = machine.start
        # The Machine's flags byte is uninitialised at exec (0x244 there, bit 8 clear), so no "0" is sent.
        machine.type(start + 100_000_000, "+")
        machine.type(start + 170_000_000, r"\D", hold_ms=1000)
        while machine.clock < 5_000_000_000 + args.seconds * machine.ips:
            elapsed = machine.clock - start
            if elapsed > 190_000_000:
                state = pilot_state(machine)
                rows.append(dict(clock=machine.clock, seconds=elapsed / machine.ips, **state))
                if tick % 50 == 0:
                    print({k: state[k] for k in ("target_range", "altitude", "speed", "throttle", "weapon",
                                                  "store_count", "launch_events", "cargo_ttl")}, flush=True)
                if impacts(rows) and machine.clock - impacts(rows)[0][1]["clock"] >= machine.ips:
                    break
                if elapsed > args.seconds * machine.ips:
                    break
                control(machine, state, tick, args.release_lo, args.release_hi)
                tick += 1
                step = machine.ips // 5
            else:
                step = 90_000
            if machine.run_until(machine.clock + step) != Machine.SLICE:
                break
        failures = errors(rows) if rows else ["no observed flight"]
        report = dict(backend="dosbox-x", clock=machine.clock, hash=None, program=machine.program,
                      errors=failures, fixes=[], release=[args.release_lo, args.release_hi],
                      observation=rows[-1] if rows else None)
        (args.out / "result.json").write_text(json.dumps(report, indent=2) + "\n")
        with (args.out / "flight.csv").open("w", newline="") as stream:
            if rows:
                writer = csv.DictWriter(stream, fieldnames=rows[0]); writer.writeheader(); writer.writerows(rows)
        print(json.dumps(report), flush=True)
    return int(bool(report["errors"]))


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--data", required=True)
    parser.add_argument("--front", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--release-lo", type=int, default=120)
    parser.add_argument("--release-hi", type=int, default=235)
    parser.add_argument("--seconds", type=int, default=1500)
    parser.add_argument("--trace-reads", type=Path, help="write the Machine's DS read set to this file and stop")
    args = parser.parse_args()
    if args.trace_reads:
        trace_reads(args.data, args.front, args.out, args.trace_reads)
        return 0
    return fly(args)


if __name__ == "__main__":
    raise SystemExit(main())
