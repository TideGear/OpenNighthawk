#!/usr/bin/env python3
"""A closed-loop pilot (cargo_pilot.py's supply drop, or --pilot strike's strike_pilot.py) flown in 86Box instead of the Machine.

86Box has no DOS-level notion of "the running program's PSP" (unlike DOSBox-X), so VGAME's data
segment is found instead by a 48-byte signature from its own loaded image (tools/ref86box/
86box-trace.patch, B86_LOOP_*): once the signature is found at load segment L, L+0x1E42 is
VGAME's DGROUP, the same DS the Machine and DOSBox-X use. The front end (SETUP, START's mission
choice and the hangar) is the recorded input of the route, scheduled as 86Box displayed-frame
events (B86_KEYS/B86_MOUSE), calibrated against a measured frame (the game's own pace sets the
calibration, not a clock this code controls). From VGAME's start the emulator pauses every 200 ms
of emulated time, dumps the data segment bytes the pilot reads (tools/routes/cargo_pilot.reads),
and waits for the keys the pilot queues for that tick. The pilot, its controls and its verdict are
cargo_pilot.py's own; this file only supplies the machine.

86Box has no C-level INT 1Ah handler to trap the way DOSBox-X's DBX_INT1A_TICK does (it runs real
BIOS ROM code), so B86_SEED_TICK restages START's mission generator instead: the first frame its
state (DS:AE8C) leaves the C runtime's srand(1), the seed tick and step count that produced it are
recovered by search and the state is replaced by the Machine's seed stepped as many times, so the
generated mission is the Machine's (TRACE/seed.txt records the substitution).

    py tools/b86_cargo_pilot.py --data GOG_DIR --front tools/routes/cargo_pilot.input --out DIR
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
import time

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

from machine_api import Machine  # noqa: E402
import cargo_pilot  # noqa: E402
import strike_pilot  # noqa: E402
import dosbox_cargo_pilot as dbx  # noqa: E402
from cargo_check import errors as cargo_errors, impacts  # noqa: E402
from dosbox_cargo_pilot import (key_bytes, read_ranges, MACHINE_PSP, DS_BASE, MEM_MASK,  # noqa: E402
                                 FRONT_END_CLOCK, PILOTS)

sys.path.insert(0, str(HERE / "ref86box"))
import probe86  # noqa: E402

EXE = r"D:\86box-src\build\src\86Box.exe"
PROFILE = r"D:\86box\vmt386"
ROMS = r"D:\86box\app\roms"
MOUSE_DRIVER = r"D:\f117-gate\ctm\CTMOUSE.EXE"
TRACE_PS1 = HERE / "ref86box" / "trace_86box.ps1"
IPS = 9_000_000
TICK_MS = 200
SETUP_CLOCK = 90000
# From a DOSBox-X run of this exact route (tools/ref86box/build_dosbox_x.md): START.EXE execs at
# Machine clock 956881664 (START's own session start), VGAME.EXE at 3371769171.
START_EXEC_CLOCK = [956881664]            # the route's own (--start-exec-clock); the strike front end is 956971536
# 86Box's own frame rate at this video mode (tools/ref86box/sav86.py; independent of CPU speed).
FPS = 70.086
# The displayed frame at which START.EXE is first found in RAM on this profile (bare boot, no
# route-specific input beyond SETUP's two answers), measured by binary search with the signature
# below against 7 Oct 2026's build: between frames 10700 and 10850.
FRAME_START_EXEC = 10775
# START's mission generator seed tick (tools/ref86box/build_dosbox_x.md's DBX_INT1A_TICK value):
# the Machine's BIOS tick count at the moment START reads it for this route.
SEED_TICK = [31579]


def front_schedule(route_lines):
    """(B86_KEYS, B86_MOUSE) for the route's front end, from START.EXE's own exec to VGAME's.

    The route file is the whole recorded flight (SETUP through the in-game pitch/release keys);
    only its SETUP-to-VGAME span is a fixed front end here, so events at or after VGAME's start
    are dropped (the flight itself is controlled live, by cargo_pilot.py's own control())."""
    events = [l.split() for l in route_lines if l and not l.startswith("#")]
    keys, mouse = [], []

    def frame(clock):
        return FRAME_START_EXEC + round(FPS * (clock - START_EXEC_CLOCK[0]) / IPS)

    pending_ext = False
    last_pos = [(-1, -1)]
    last_buttons = [0]
    last_move_frame = [0]
    for p in events:
        clock = int(p[1])
        if clock < START_EXEC_CLOCK[0] or clock >= FRONT_END_CLOCK[0]:
            continue
        f = frame(clock)
        if p[0] == "K":
            byte = int(p[2], 16)
            if byte == 0xE0:
                pending_ext = True                           # remembered for the next byte
                continue
            down = not (byte & 0x80)
            scan = byte & 0x7F
            if pending_ext:
                scan |= 0xE000
                pending_ext = False
            keys.append("%d:%d:%x" % (f, 1 if down else 0, scan))
        else:
            x, y, buttons = int(p[2]), int(p[3]), int(p[4])
            if (x, y) != last_pos[0]:
                mouse.append("%d:m:%d,%d" % (f, x, y))
                last_pos[0] = (x, y)
                last_move_frame[0] = f
            if buttons != last_buttons[0]:
                # B86_MOUSE's "m" is a slam to the corner then a throttled scaled move, not an
                # instant jump: the cursor is not at (x, y) until move_frame+30 (tools/ref86box/
                # build_86box.md). save_parity.py's own proven clicks press +40/+55 after the move
                # for exactly this reason; the route's own recorded down/up spacing assumed an
                # instant cursor and would click mid-transit.
                offset = 40 if buttons else 55
                mouse.append("%d:b:%d" % (max(f, last_move_frame[0] + offset), buttons))
                last_buttons[0] = buttons
    return ",".join(keys), ";".join(mouse)


class B86Machine:
    """The subset of machine_api.Machine that cargo_pilot's controls and observers use."""

    SLICE = Machine.SLICE
    EXITED = Machine.EXITED
    ips = IPS
    psp = MACHINE_PSP

    def __init__(self, data, front_route, out, time_us, turbo=True):
        self.out = out
        self.state_path = out / "loop.state"
        self.reply_prefix = str(out / "reply.")
        self.ranges = read_ranges()
        work = os.path.normpath(str(out / "profile"))
        os.makedirs(work)
        cfg = open(os.path.join(PROFILE, "86box.cfg")).read()
        cfg = re.sub(r"(?m)^mouse_type\s*=.*$", "mouse_type = msserial", cfg)
        open(os.path.join(work, "86box.cfg"), "w").write(cfg)
        shutil.copytree(os.path.join(PROFILE, "nvr"), os.path.join(work, "nvr"))
        subprocess.run(["cmd", "/c", "mklink", "/J", os.path.join(work, "roms"), ROMS],
                       stdout=subprocess.DEVNULL, check=True)
        img = os.path.join(work, "f117a.img")
        shutil.copyfile(os.path.join(PROFILE, "f117a.img"), img)
        driver = open(MOUSE_DRIVER, "rb").read()

        def install(fs):
            fs.writebytes("/F117A/CTMOUSE.EXE", driver)
            probe86.bare_boot(fs, ["CTMOUSE", "F117"])
        probe86.with_partition(img, install, write=True)
        keys, mouse = front_schedule(front_route)
        setup = "3000:1:31,3003:0:31,3300:1:03,3303:0:03"
        keys_file = out / "keys.txt"
        keys_file.write_text(setup + ("," + keys if keys else ""))
        mouse_file = out / "mouse.txt"
        mouse_file.write_text(mouse)
        env = dict(os.environ, B86_LOOP_STATE=str(self.state_path), B86_LOOP_REPLY=self.reply_prefix,
                   B86_LOOP_EVERY=str(TICK_MS), B86_LOOP_READS=",".join("0x%x:%d" % r for r in self.ranges),
                   B86_KEYS_FILE=str(keys_file), B86_MOUSE_FILE=str(mouse_file), B86_SEED_TICK=str(SEED_TICK[0]))
        if turbo:
            env["B86_FAST"] = "1"
        trace = out / "trace"
        self.proc = subprocess.Popen(
            ["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", str(TRACE_PS1),
             "-Profile", work, "-Out", str(trace), "-Stop", "99999999", "-TimeoutSeconds", "3600",
             "-Exe", EXE, "-Roms", ROMS],
            env=env)
        self.dump = {}
        self.offset = 0
        self.buffer = b""
        self.queue = []
        self.seq = 0
        self.pending = []
        self.clock = None
        self.base = None
        self.program = "VGAME.EXE"
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
                raise RuntimeError("86Box exited before the next tick")
            if time.time() > deadline:
                raise TimeoutError("no 86Box tick")
            time.sleep(0.001)
        parts = self.queue.pop(0).split()
        self.seq = int(parts[0])
        ms = float(parts[1])
        self.clock = round(ms * IPS / 1000)
        live = parts[6].split("=")[1] == "1"
        self.program = "VGAME.EXE" if live else "DSWAP.EXE"
        data = [int(b, 16) for b in parts[7:]]
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
            raise KeyError("read at DS+0x%x is outside the 86Box read set" % off)
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
        self._reply(";".join(self.pending))
        self.pending = []
        self._take_state()
        return self.SLICE if self.program == "VGAME.EXE" else self.EXITED

    def _reply(self, spec):
        final = "%s%d" % (self.reply_prefix, self.seq)
        with open(final + ".tmp", "w") as f:
            f.write(spec + "\n")
        os.replace(final + ".tmp", final)
        # 86Box deletes the previous reply itself once it has read the next one.
        try:
            os.remove("%s%d" % (self.reply_prefix, self.seq - 1))
        except FileNotFoundError:
            pass

    def screen(self, path):
        return None

    def close(self):
        if self.proc.poll() is None:
            self._reply("stop")
            try:
                self.proc.wait(timeout=30)
            except subprocess.TimeoutExpired:
                self.proc.kill()


def fly(args):
    strike = args.pilot == "strike"
    pilot_state = strike_pilot.strike_state if strike else cargo_pilot.pilot_state
    route = args.front.read_text().splitlines()
    header = re.fullmatch(r"# f117r-input ips=9000000 time_us=(\d+)", route[0] if route else "")
    if not header:
        raise ValueError("the front file needs its recorded start clock header")
    args.out.mkdir(parents=True, exist_ok=False)
    if strike:
        strike_pilot.RELEASE_RANGE[0] = args.release_range
        strike_pilot.SELECT_KEY[0] = args.select_key
        strike_pilot.SELECT_EVERY[0] = args.select_every
    rows, tick, initialized = [], 0, False
    with B86Machine(args.data, route, args.out, int(header[1]), not args.realtime) as machine:
        start = machine.start
        if not strike:
            machine.type(start + 100_000_000, "+")
            machine.type(start + 170_000_000, r"\D", hold_ms=1000)
        while machine.clock < 5_000_000_000 + args.seconds * machine.ips:
            elapsed = machine.clock - start
            if strike and not initialized and elapsed > 40_000_000:
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
        report = dict(backend="86box", clock=machine.clock, hash=None, program=machine.program,
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
    parser.add_argument("--front", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--release-lo", type=int, default=60)
    parser.add_argument("--release-hi", type=int, default=300)
    parser.add_argument("--release-range", type=int, default=80, help="strike: release the bomb this close")
    parser.add_argument("--select-key", default="n", help="strike: n (next target) or b (drop lock)")
    parser.add_argument("--select-every", type=int, default=2, help="strike: press the select key every this many ticks until designated")
    parser.add_argument("--front-end-clock", type=int, help="the Machine clock of VGAME's exec in the front")
    parser.add_argument("--start-exec-clock", type=int, help="the Machine clock of START's exec in the front")
    parser.add_argument("--seed-tick", type=int, help="START's seed tick on the Machine")
    parser.add_argument("--seconds", type=int, default=1500)
    parser.add_argument("--realtime", action="store_true", help="pace 86Box to real time")
    args = parser.parse_args()
    dbx.PILOT[0] = args.pilot
    dbx.READS[0] = HERE / "routes" / PILOTS[args.pilot]["reads"]
    if args.front_end_clock:
        FRONT_END_CLOCK[0] = args.front_end_clock
    if args.start_exec_clock:
        START_EXEC_CLOCK[0] = args.start_exec_clock
    if args.seed_tick:
        SEED_TICK[0] = args.seed_tick
    return fly(args)


if __name__ == "__main__":
    raise SystemExit(main())
