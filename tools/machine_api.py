"""Read-only guest observations and normal input through f117machine_api.

Build with F117R_BUILD_TESTS=ON. One machine may be open per process.
All input times and run limits are absolute instruction clocks.
"""
from __future__ import annotations

import ctypes as C
import os
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def library_path():
    # F117R_MACHINE_API names another build's library (one built elsewhere
    # while this checkout's is in use).
    override = os.environ.get("F117R_MACHINE_API")
    if override:
        return Path(override)
    for folder in (ROOT / "build", ROOT / "build" / "Release"):
        for name in ("f117machine_api.dll", "libf117machine_api.so", "libf117machine_api.dylib"):
            path = folder / name
            if path.is_file():
                return path
    raise FileNotFoundError("build f117machine_api with F117R_BUILD_TESTS=ON")


class Machine:
    SLICE, EXITED, FAULT = 0, 1, 2

    def __init__(self, data, save, *, log=None, ips=9_000_000,
                 time_us=700_000_000_000_000, engine="interp", library=None, fixes=()):
        if engine not in ("interp", "recomp"):
            raise ValueError("engine must be interp or recomp")
        self.dll = C.CDLL(str(library or library_path()))
        signatures = {
            "open": (C.c_void_p, [C.c_char_p, C.c_char_p, C.c_char_p, C.c_uint64, C.c_uint64, C.c_int]),
            "close": (None, [C.c_void_p]), "error": (C.c_char_p, [C.c_void_p]),
            "run": (C.c_int, [C.c_void_p, C.c_uint64]),
            "clock": (C.c_uint64, [C.c_void_p]), "hash": (C.c_uint64, [C.c_void_p]),
            "program": (C.c_char_p, [C.c_void_p]), "start": (C.c_uint64, [C.c_void_p]),
            "psp": (C.c_uint16, [C.c_void_p]),
            "read8": (C.c_uint8, [C.c_void_p, C.c_uint32]),
            "read16": (C.c_uint16, [C.c_void_p, C.c_uint32]),
            "read32": (C.c_uint32, [C.c_void_p, C.c_uint32]),
            "key": (C.c_int, [C.c_void_p, C.c_uint64, C.c_uint8]),
            "type": (C.c_int, [C.c_void_p, C.c_uint64, C.c_uint64, C.c_uint64, C.c_char_p]),
            "mouse": (C.c_int, [C.c_void_p, C.c_uint64, C.c_int, C.c_int, C.c_uint]),
            "record": (C.c_int, [C.c_void_p, C.c_char_p]),
            "screen": (C.c_int, [C.c_void_p, C.c_char_p]),
            "fix": (C.c_int, [C.c_void_p, C.c_char_p, C.c_int]),
            "stage_write16": (None, [C.c_void_p, C.c_uint32, C.c_uint16]),
        }
        self.fn = {}
        for name, (result, args) in signatures.items():
            # A library built before fixes existed lacks the switch; it can
            # still run every unfixed machine.
            if name in ("fix", "stage_write16") and not hasattr(self.dll, "f117_machine_" + name):
                continue
            fn = getattr(self.dll, "f117_machine_" + name)
            fn.restype, fn.argtypes = result, args
            self.fn[name] = fn
        self.ips = ips
        Path(save).mkdir(parents=True, exist_ok=True)
        self.handle = self.fn["open"](self._path(data), self._path(save), self._path(log),
                                      ips, time_us, int(engine == "recomp"))
        if not self.handle:
            raise RuntimeError(self.fn["error"](None).decode("utf-8", "replace"))
        # Switchable fixes (docs/bugs.md); every one is off unless named.
        for fix in fixes:
            if "fix" not in self.fn or not self.fn["fix"](self.handle, fix.encode(), 1):
                self.close()
                raise ValueError("no fix " + fix)

    @staticmethod
    def _path(path):
        # fopen uses the Windows ANSI code page; UTF-8 on other platforms.
        import os
        return str(path).encode("mbcs" if os.name == "nt" else "utf-8") if path is not None else None

    def _call(self, name, *args):
        if not self.handle:
            raise RuntimeError("machine is closed")
        return self.fn[name](self.handle, *args)

    def close(self):
        if self.handle:
            self.fn["close"](self.handle)
            self.handle = None

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()

    @property
    def clock(self):
        return self._call("clock")

    @property
    def hash(self):
        return self._call("hash")

    @property
    def program(self):
        return self._call("program").decode("ascii")

    @property
    def start(self):
        return self._call("start")

    @property
    def psp(self):
        return self._call("psp")

    def run_until(self, until):
        if until < self.clock:
            raise ValueError("run limit is in the past")
        status = self._call("run", until)
        if status == self.FAULT:
            raise RuntimeError(self._call("error").decode("utf-8", "replace"))
        return status

    def read8(self, address):
        return self._call("read8", address)

    def read16(self, address):
        return self._call("read16", address)

    def stage_write16(self, address, value):
        """STAGING ONLY: write one guest word, for a fix check that needs a
        state normal play takes hours to build. Never for pilots or parity
        observers; a run that calls it is a staged run."""
        self.staged = True
        return self._call("stage_write16", address, value)

    def read32(self, address):
        return self._call("read32", address)

    def _input(self, name, at, *args):
        if at < self.clock:
            raise ValueError("input time is in the past")
        if not self._call(name, at, *args):
            raise RuntimeError("cannot queue input: " + self._call("error").decode("utf-8", "replace"))

    def key(self, at, scan):
        if not 0 <= scan <= 255:
            raise ValueError("scan code must be a byte")
        self._input("key", at, scan)

    def type(self, at, keys, *, hold_ms=60, gap_ms=60):
        if hold_ms < 0 or gap_ms < 0:
            raise ValueError("input duration must be nonnegative")
        self._input("type", at, self.ips * hold_ms // 1000,
                    self.ips * gap_ms // 1000, keys.encode("ascii"))

    def mouse(self, at, x, y, buttons=0):
        self._input("mouse", at, x, y, buttons)

    def record(self, path):
        if not self._call("record", self._path(path)):
            raise RuntimeError("recording must start at clock zero to a writable file")

    def screen(self, path):
        if not self._call("screen", self._path(path)):
            raise OSError("cannot write screen")


class RouteInputs:
    """Queue route inputs when their named program first becomes visible.

    Call poll after each short run slice. Slices must be shorter than the
    earliest program-relative input; a missed deadline raises ValueError.
    """

    def __init__(self, args, hold_ms=60):
        self.pending = []
        self.hold_ms = hold_ms
        for option, value in zip(args[::2], args[1::2]):
            if option == "--hold":
                self.hold_ms = int(value)
            elif option in ("--type", "--click", "--move"):
                when, content = value.split(":", 1)
                if "+" in when:
                    program, offset = when.split("+", 1)
                else:
                    program, offset = "", when
                self.pending.append((program.upper(), int(offset), option, content))

    def poll(self, machine):
        program, start = machine.program.upper(), machine.start
        remaining = []
        for name, offset, option, content in self.pending:
            if name and name != program:
                remaining.append((name, offset, option, content))
                continue
            at = (start if name else 0) + offset
            if option == "--type":
                hold = self.hold_ms
                if content.startswith("~"):
                    duration, content = content[1:].split(":", 1)
                    hold = int(duration)
                machine.type(at, content, hold_ms=hold, gap_ms=self.hold_ms)
            else:
                x, y = map(int, content.split(","))
                machine.mouse(at, x, y)
                if option == "--click":
                    hold = machine.ips * self.hold_ms // 1000
                    machine.mouse(at + hold, x, y, 1)
                    machine.mouse(at + 3 * hold, x, y, 0)
        self.pending = remaining
