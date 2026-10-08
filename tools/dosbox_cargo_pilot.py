#!/usr/bin/env python3
"""The closed-loop supply drop of cargo_pilot.py, flown in the DOSBox-X reference instead of the Machine.

The front end (SETUP, START's mission choice and the hangar) is the recorded input of the route, given
to DOSBox-X as raw scancodes and driver positions in emulated time. From VGAME's start the emulator
pauses every 200 ms, dumps the data segment bytes the pilot reads (tools/routes/cargo_pilot.reads), and
waits for the keys and clicks the pilot queues for that tick. The pilot, its controls and its verdict
are cargo_pilot.py's own; this file only supplies the machine. DOSBox-X runs in fast-forward unless
--realtime is given: emulated time is still 9,000 cycles per ms, and both give identical ticks.

    py tools/dosbox_cargo_pilot.py --data GOG_DIR --front tools/routes/cargo_pilot.input --out DIR
    py tools/dosbox_cargo_pilot.py --data GOG_DIR --front tools/routes/cargo_pilot.input --out DIR --trace-reads FILE
"""
import argparse
import csv
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import time

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

from machine_api import Machine  # noqa: E402
import cargo_pilot  # noqa: E402
import strike_pilot  # noqa: E402
from cargo_check import errors as cargo_errors, impacts  # noqa: E402

DOSBOX = "D:/86box-src/dbx-src/src/dosbox-x.exe"
READS = [HERE / "routes" / "cargo_pilot.reads"]
MACHINE_PSP = 6506
DS_BASE = ((MACHINE_PSP + 0x10 + 0x1E42) << 4)
MEM_MASK = 0xFFFFF
IPS = 9_000_000
FRONT_END_CLOCK = [3371769171]
TICK_MS = 200
SETUP_CLOCK = 90000
# START seeds its mission generator (srand at 0x96BC) from the BIOS tick count read at 0x8607, called
# from 0x7379, as requestr.pic's decode ends. That moment is within a few ms of a tick on both
# emulators, so the Machine's value (31579 on this route) is staged rather than left to timing.
START_SEED = ["0x860B,0x737C,31579"]


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
        if clock >= FRONT_END_CLOCK[0]:
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
    for line in READS[0].read_text().splitlines():
        if line.strip() and not line.startswith("#"):
            off, length = line.split()
            ranges.append((int(off, 0), int(length, 0)))
    # The flight's event list grows as the flight goes on, and the Machine's trace saw only its own length.
    ranges.extend(EXTRA_READS.get(PILOT[0], []))
    return ranges


class DosboxMachine:
    """The subset of machine_api.Machine that cargo_pilot's controls and observers use."""

    SLICE = Machine.SLICE
    EXITED = Machine.EXITED
    ips = IPS
    psp = MACHINE_PSP

    def __init__(self, data, front_route, out, time_us, turbo=False):
        self.out = out
        self.state_path = out / "loop.state"
        self.reply_prefix = str(out / "reply.")
        self.ranges = read_ranges()
        # The game saves into the folder it is mounted from (START writes ROSTER.FIL in a front end that
        # creates a pilot), so DOSBox-X gets a private copy and never the install.
        game = out / "game"
        shutil.copytree(data, game, ignore=shutil.ignore_patterns("DOSBOX", "cloud_saves"))
        conf = out / "loop.conf"
        conf.write_text("\n".join([
            "[sdl]", "fullscreen=false", "output=surface", "[mixer]", "nosound=true",
            # Fast-forward drops the real-time sleeps; emulated time is still cycles / 9000 per ms.
            "[cpu]", "turbo=%s" % str(turbo).lower(), "stop turbo on key=false",
            "[autoexec]", "@echo off", 'mount C "%s"' % game, "c:", "keyb us", "cls", "f117", "exit", ""]))
        env = dict(os.environ, SDL_VIDEODRIVER="dummy",
                   DBX_AUTO_INPUT_AT="SETUP", DBX_AUTO_INPUT=front_spec(front_route),
                   DBX_WALL_US=str(time_us + SETUP_CLOCK * 1_000_000 // IPS), DBX_INT1A_TICK=START_SEED[0],
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
        self.queue = []
        self.seq = 0
        self.pending = []
        self.clock = None
        self.base = None
        self.vgame_ms = None
        self.log_offset = 0
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
                data = f.read()
            self.offset += len(data)
            self.buffer += data
        end = self.buffer.rfind(b"\n")
        if end < 0:
            return []
        complete, self.buffer = self.buffer[:end + 1], self.buffer[end + 1:]
        return complete.decode().splitlines()

    def _take_state(self):
        deadline = time.time() + 7200
        while True:
            self.queue.extend(self._new_lines())
            if self.queue:
                break
            if self.proc.poll() is not None:
                raise RuntimeError("DOSBox-X exited before the next tick")
            if time.time() > deadline:
                raise TimeoutError("no DOSBox-X tick")
            time.sleep(0.001)
        parts = self.queue.pop(0).split()
        self.seq = int(parts[0])
        ms = float(parts[1])
        if self.base is None:
            self.base = FRONT_END_CLOCK[0] - round(ms * IPS / 1000)
            self.vgame_ms = ms
        self.clock = round(ms * IPS / 1000) + self.base
        # A program executed after VGAME (DSWAP, END) means the flight is over and the data segment reused.
        with open(self.out / "auto.log", "rb") as log:
            log.seek(self.log_offset)
            text = log.read()
        text = text[:text.rfind(b"\n") + 1]
        self.log_offset += len(text)
        for line in text.decode().splitlines():
            p = line.split()
            if len(p) >= 3 and p[1] == "exec" and self.vgame_ms < float(p[0]) <= ms:
                self.program = p[2].rsplit("\\", 1)[-1].upper()
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
            if os.environ.get("F117_DBX_LENIENT"):
                return 0                        # a seed scan reads only the mission identifiers
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
        return self.SLICE if self.program == "VGAME.EXE" else self.EXITED

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


def _cargo_main():
    return cargo_pilot.main


def _strike_main():
    return strike_pilot.main


PILOTS = {
    "cargo": dict(trace_main=_cargo_main, reads="cargo_pilot.reads"),
    "strike": dict(trace_main=_strike_main, reads="strike_training.reads"),
}
PILOT = ["cargo"]
EXTRA_READS = {"strike": [(0xba5a, 6 * 255)]}


def trace_reads(pilot_argv, target):
    seen = set()
    orig8, orig16 = Machine.read8, Machine.read16

    def read8(self, address):
        seen.add((address, 1))
        return orig8(self, address)

    def read16(self, address):
        seen.add((address, 2))
        return orig16(self, address)

    Machine.read8, Machine.read16 = read8, read16
    sys.argv = [PILOT[0] + "_pilot.py"] + pilot_argv
    pilot_main = PILOTS[PILOT[0]]["trace_main"]()
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
        target.write_text("# DS-relative bytes read by the %s pilot on the Machine: offset length\n" % PILOT[0] +
                          "".join("0x%x %d\n" % r for r in ranges))
        print("read set", len(ranges), "ranges,", sum(n for _, n in ranges), "bytes", flush=True)


def fly(args):
    strike = PILOT[0] == "strike"
    pilot_state = strike_pilot.strike_state if strike else cargo_pilot.pilot_state
    route = args.front.read_text().splitlines()
    header = re.fullmatch(r"# f117r-input ips=9000000 time_us=(\d+)", route[0] if route else "")
    if not header:
        raise ValueError("the front file needs its recorded start clock header")
    args.out.mkdir(parents=True, exist_ok=False)
    if strike:
        strike_pilot.RELEASE_RANGE[0] = args.release_range
    rows, tick, initialized = [], 0, False
    with DosboxMachine(args.data, route, args.out, int(header[1]), not args.realtime) as machine:
        start = machine.start
        if not strike:
            # The Machine's flags byte is uninitialised at exec (0x244 there, bit 8 clear), so no "0" is sent.
            machine.type(start + 100_000_000, "+")
            machine.type(start + 170_000_000, r"\D", hold_ms=1000)
        while machine.clock < 5_000_000_000 + args.seconds * machine.ips:
            elapsed = machine.clock - start
            if strike and not initialized and elapsed > 40_000_000:
                # The strike pilot reads the brake flag after 40 ms, as strike_pilot.py does.
                if pilot_state(machine)["flags"] & 8:
                    machine.type(start + 80_000_000, "0")
                machine.type(start + 100_000_000, "+")
                machine.type(start + 170_000_000, r"\D", hold_ms=1000)
                initialized = True
            if elapsed > 190_000_000:
                state = pilot_state(machine)
                rows.append(dict(clock=machine.clock, seconds=elapsed / machine.ips, **state))
                if tick % 50 == 0:
                    print({k: state[k] for k in ("target_range", "altitude", "speed", "throttle", "weapon",
                                                  "store_count", "launch_events")}, flush=True)
                if strike:
                    if state["flags"] & 0x4000:
                        # As strike_pilot.py: observe one more second after the credit.
                        machine.run_until(machine.clock + machine.ips)
                        rows.append(dict(clock=machine.clock, seconds=(machine.clock - start) / machine.ips,
                                         **pilot_state(machine)))
                        break
                elif impacts(rows) and machine.clock - impacts(rows)[0][1]["clock"] >= machine.ips:
                    break
                if elapsed > args.seconds * machine.ips:
                    break
                if strike:
                    strike_pilot.control(machine, state, tick)
                else:
                    cargo_pilot.control(machine, state, tick, args.release_lo, args.release_hi)
                tick += 1
                step = machine.ips // 5
            else:
                step = 90_000
            if machine.run_until(machine.clock + step) != Machine.SLICE:
                break
        failures = (strike_pilot.errors(rows) if strike else cargo_errors(rows)) if rows else ["no observed flight"]
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
    parser.add_argument("--pilot", choices=sorted(PILOTS), default="cargo")
    parser.add_argument("--data", required=True)
    parser.add_argument("--front", type=Path, help="a recorded input whose front end (before VGAME) is replayed")
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--release-lo", type=int, default=60)
    parser.add_argument("--release-hi", type=int, default=300)
    parser.add_argument("--release-range", type=int, default=120, help="strike: release the bomb this close")
    parser.add_argument("--front-end-clock", type=int, help="the Machine clock of VGAME's exec in the front")
    parser.add_argument("--seed-tick", type=int, help="START's seed tick on the Machine (staged by DBX_INT1A_TICK)")
    parser.add_argument("--seconds", type=int, default=1500)
    parser.add_argument("--realtime", action="store_true",
                        help="pace DOSBox-X to real time (fast-forward gives identical ticks, about 8x faster)")
    parser.add_argument("--trace-reads", type=Path,
                        help="write the Machine's DS read set to this file and stop; the pilot's own arguments follow --")
    args, rest = parser.parse_known_args()
    PILOT[0] = args.pilot
    READS[0] = HERE / "routes" / PILOTS[args.pilot]["reads"]
    if args.front_end_clock:
        FRONT_END_CLOCK[0] = args.front_end_clock
    if args.seed_tick:
        START_SEED[0] = "0x860B,0x737C,%d" % args.seed_tick
    if args.trace_reads:
        trace_reads([a for a in rest if a != "--"], args.trace_reads)
        return 0
    if rest or not args.front:
        parser.error("--front is required")
    return fly(args)


if __name__ == "__main__":
    raise SystemExit(main())
