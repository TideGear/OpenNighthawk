#!/usr/bin/env python3
"""Observe an unmodified GOG DOSBox flight through read-only process memory.

Inputs use the window's normal keyboard/mouse path. Timing is wall time,
not a claim of instruction-clock alignment. All game copies and observations
stay in --out. No guest or process memory is written.
"""
import argparse
import ctypes as C
from ctypes import wintypes as W
import csv
from datetime import datetime, timezone
import json
from pathlib import Path
import shutil
import struct
import subprocess
import time
import os

from landing_pilot import observe, control as landing_control, signed
from random_flights import base_route
from run_route import route_args, check_route


class GuestMemory:
    def __init__(self, pid):
        self.k = C.WinDLL("kernel32", use_last_error=True)
        self.k.OpenProcess.argtypes = [W.DWORD, W.BOOL, W.DWORD]
        self.k.OpenProcess.restype = W.HANDLE
        self.k.ReadProcessMemory.argtypes = [W.HANDLE, C.c_void_p, C.c_void_p, C.c_size_t, C.POINTER(C.c_size_t)]
        self.k.VirtualQueryEx.argtypes = [W.HANDLE, C.c_void_p, C.c_void_p, C.c_size_t]
        self.k.VirtualQueryEx.restype = C.c_size_t
        self.k.CloseHandle.argtypes = [W.HANDLE]
        self.handle = self.k.OpenProcess(0x410, False, pid)  # QUERY_INFORMATION | VM_READ
        if not self.handle:
            raise C.WinError(C.get_last_error())
        self.base = None
        try:
            self.base = self.find_guest()
        except BaseException:
            self.close()
            raise

    def close(self):
        if self.handle:
            self.k.CloseHandle(self.handle)
            self.handle = None

    def read(self, address, count):
        buf = C.create_string_buffer(count)
        n = C.c_size_t()
        if not self.k.ReadProcessMemory(self.handle, address, buf, count, C.byref(n)):
            raise C.WinError(C.get_last_error())
        if n.value != count:
            raise RuntimeError("short process-memory read")
        return buf.raw

    def find_guest(self):
        class Region(C.Structure):
            _fields_ = [("base", C.c_void_p), ("allocation", C.c_void_p),
                        ("allocation_protect", W.DWORD), ("size", C.c_size_t),
                        ("state", W.DWORD), ("protect", W.DWORD), ("type", W.DWORD)]
        address = 0
        signature = b"IBM COMPATIBLE 486 BIOS COPYRIGHT The DOSBox Team."
        while address < 0x80000000:
            region = Region()
            if not self.k.VirtualQueryEx(self.handle, address, C.byref(region), C.sizeof(region)):
                break
            if region.state == 0x1000 and not region.protect & 0x101 and region.protect & 0xEE:
                # The 16 MB guest allocation is contiguous. Search overlapping
                # chunks so neither a page nor chunk boundary hides the anchor.
                for offset in range(0, region.size, 4 * 1024 * 1024):
                    try:
                        data = self.read(region.base + offset, min(4 * 1024 * 1024 + 64, region.size - offset))
                    except OSError:
                        continue
                    pos = data.find(signature)
                    while pos >= 0:
                        candidate = region.base + offset + pos - 0xFE00E
                        try:
                            if candidate > 0 and self.read(candidate + 0xFFFF5, 8) == b"01/01/92":
                                return candidate
                        except OSError:
                            pass
                        pos = data.find(signature, pos + 1)
            address = (region.base or 0) + region.size
        raise RuntimeError("DOSBox guest-memory anchor not found")

    def refresh(self):
        self.ram = self.read(self.base, 0x100000)
        self.psp = find_psp(self.ram, b"VGAME")
        if self.psp is None:
            return None
        state = observe(self)
        state["bios_ticks"] = self.read32(0x46C)
        ds = (self.psp + 0x10 + 0x1E42) << 4
        for name, offset in {"primary_type": 0xE304, "primary_target": 0xE306,
                             "secondary_type": 0xE316, "events": 0x951A,
                             "frame": 0x3D8E, "rng": 0x929E, "mission_time": 0x9912}.items():
            state[name] = self.read16(ds + offset)
        return state

    def read8(self, address):
        return self.ram[address & 0xFFFFF]

    def read16(self, address):
        return self.read8(address) | self.read8(address + 1) << 8

    def read32(self, address):
        return self.read16(address) | self.read16(address + 2) << 16


def find_psp(ram, name):
    """An owned, paragraph-aligned live MCB followed by a valid PSP."""
    pos = ram.find(name)
    while pos >= 0:
        mcb = pos - 8
        if (mcb >= 0 and mcb % 16 == 0 and ram[pos + len(name):pos + 8].strip(b" \0") == b""
                and ram[mcb] in (ord("M"), ord("Z"))):
            psp = mcb // 16 + 1
            if struct.unpack_from("<H", ram, mcb + 1)[0] == psp and ram[mcb + 16:mcb + 18] == b"\xcd\x20":
                return psp
        pos = ram.find(name, pos + 1)
    return None



def load_input_log(path):
    """Return the recorded IPs and ordered normal keyboard/mouse events.

    Extended set-1 keys are stored as an E0 prefix followed by the scan byte;
    pair them here so the window message can carry Windows' extended-key bit.
    """
    lines = Path(path).read_text().splitlines()
    if not lines or not lines[0].startswith("# f117r-input "):
        raise ValueError("not an f117r input log")
    fields = dict(item.split("=", 1) for item in lines[0].split()[2:] if "=" in item)
    ips = int(fields.get("ips", "0"))
    if ips != 9_000_000:
        raise ValueError("DOSBox replay requires the recorded 9 MIPS clock")
    time_us = int(fields.get("time_us", "0"))
    if time_us <= 0:
        raise ValueError("input log has no valid startup timestamp")
    events = []
    extended_at = None
    for line_number, line in enumerate(lines[1:], 2):
        parts = line.split()
        if not parts or parts[0].startswith("#"):
            continue
        if parts[0] == "K" and len(parts) == 3:
            at, code = int(parts[1]), int(parts[2], 16)
            if extended_at is not None:
                if at != extended_at:
                    raise ValueError(f"orphan E0 keyboard prefix at line {line_number}")
                events.append((at, "K", code, True))
                extended_at = None
            elif code == 0xE0:
                extended_at = at
            else:
                events.append((at, "K", code, False))
        elif parts[0] == "M" and len(parts) == 7:
            at, x, y, buttons, dx, dy = map(int, parts[1:])
            if dx or dy or buttons not in (0, 1):
                raise ValueError(f"unsupported mouse event at line {line_number}")
            events.append((at, "M", x, y, buttons))
        else:
            raise ValueError(f"unsupported input event at line {line_number}: {line}")
    if extended_at is not None:
        raise ValueError("input log ends with an orphan E0 keyboard prefix")
    if not events:
        raise ValueError("input log contains no events")
    if any(a[0] > b[0] for a, b in zip(events, events[1:])):
        raise ValueError("input events are not in clock order")
    return ips, time_us, events


def clock_setter_com(time_us):
    """Seed DOS time and virtualize INT 1Ah's date/time for one DOSBox run."""
    stamp = datetime.fromtimestamp(time_us / 1_000_000, timezone.utc)
    centiseconds = (time_us // 10_000) % 100
    seconds = stamp.hour * 3600 + stamp.minute * 60 + stamp.second
    ticks = ((seconds * 1000 + centiseconds * 10) * 1_193_182) // 65_536 // 1000
    if not 1980 <= stamp.year <= 2099 or ticks >= 1_573_040:
        raise ValueError("startup clock is outside the DOS date range")
    def bcd(value):
        return (value // 10 << 4) | (value % 10)

    rtc_values = (bcd(stamp.year // 100), bcd(stamp.year % 100),
                  bcd(stamp.month), bcd(stamp.day), bcd(stamp.hour),
                  bcd(stamp.minute), bcd(stamp.second))
    weekday = stamp.isoweekday() % 7 + 1  # DOSBox CMOS: Sunday=1 through Saturday=7

    fields = {}
    data = bytearray()
    for name, size, initial in (
            ("date_status", 1, b"\xff"), ("year", 2, b"\0\0"),
            ("month", 1, b"\0"), ("day", 1, b"\0"),
            ("hour", 1, b"\0"), ("minute", 1, b"\0"),
            ("second", 1, b"\0"), ("centisecond", 1, b"\0"),
            ("ticks", 4, struct.pack("<I", ticks)),
            ("rtc_century", 1, b"\0"), ("rtc_year", 1, b"\0"),
            ("rtc_month", 1, b"\0"), ("rtc_day", 1, b"\0"),
            ("rtc_hour", 1, b"\0"), ("rtc_minute", 1, b"\0"),
            ("rtc_second", 1, b"\0"),
            ("old_vector_offset", 2, b"\0\0"),
            ("old_vector_segment", 2, b"\0\0")):
        fields[name] = len(data)
        data.extend(initial.ljust(size, b"\0"))

    code = bytearray()
    patches = []
    relative_patches = []
    labels = {}

    def absolute(opcode, name):
        code.extend(opcode)
        patches.append((len(code), name))
        code.extend(b"\0\0")

    def branch(opcode, name):
        code.extend(opcode)
        relative_patches.append((len(code), name))
        code.append(0)

    # Seed the DOS date and BDA ticks. DOSBox 0.74 ignores writes to CMOS
    # date/time, so install a resident INT 1Ah hook for RTC read functions.
    # All other INT 1Ah services chain to the original BIOS handler.
    code.extend((0xB4, 0x2B, 0xB9))
    code.extend(struct.pack("<H", stamp.year))
    code.extend((0xBA, stamp.day, stamp.month, 0xCD, 0x21))
    absolute(b"\xA2", "date_status")       # save AL
    code.extend((0xB4, 0x01, 0xB9))
    code.extend(struct.pack("<H", ticks >> 16))
    code.extend((0xBA,))
    code.extend(struct.pack("<H", ticks & 0xFFFF))
    code.extend((0xCD, 0x1A))

    # Get and save the old vector, then install the private handler.
    code.extend((0xB8, 0x1A, 0x35, 0xCD, 0x21))  # AX=351Ah; get vector
    absolute(b"\x2E\x89\x1E", "old_vector_offset")  # save BX
    absolute(b"\x2E\x8C\x06", "old_vector_segment") # save ES
    code.extend((0x0E, 0x1F, 0xBA))  # push CS / pop DS / mov DX, handler
    patches.append((len(code), "handler")); code.extend(b"\0\0")
    code.extend((0xB8, 0x1A, 0x25, 0xCD, 0x21))  # AX=251Ah; set vector

    code.extend((0xB4, 0x2A, 0xCD, 0x21))
    absolute(b"\x89\x0E", "year")          # save CX
    absolute(b"\x88\x36", "month")        # save DH
    absolute(b"\x88\x16", "day")          # save DL
    code.extend((0xB4, 0x2C, 0xCD, 0x21))
    absolute(b"\x88\x2E", "hour")         # save CH
    absolute(b"\x88\x0E", "minute")       # save CL
    absolute(b"\x88\x36", "second")       # save DH
    absolute(b"\x88\x16", "centisecond")  # save DL
    code.extend((0xB4, 0x04, 0xCD, 0x1A))
    absolute(b"\x88\x2E", "rtc_century")  # save CH
    absolute(b"\x88\x0E", "rtc_year")     # save CL
    absolute(b"\x88\x36", "rtc_month")    # save DH
    absolute(b"\x88\x16", "rtc_day")      # save DL
    code.extend((0xB4, 0x02, 0xCD, 0x1A))
    absolute(b"\x88\x2E", "rtc_hour")    # save CH
    absolute(b"\x88\x0E", "rtc_minute")  # save CL
    absolute(b"\x88\x36", "rtc_second")  # save DH
    code.extend((0xB4, 0x3C, 0x31, 0xC9, 0xBA))  # create/truncate result file
    patches.append((len(code), "filename"))
    code.extend(b"\0\0\xCD\x21\x89\xC3")      # INT 21h; BX = handle
    code.extend((0xB4, 0x40, 0xB9))               # write 20-byte report
    code.extend(struct.pack("<H", 20))
    code.extend((0xBA,))
    patches.append((len(code), "data"))
    code.extend(b"\0\0\xCD\x21\xB4\x3E\xCD\x21")  # close file
    # Report creation takes enough guest time to cross a BIOS tick on some
    # hosts. Reset the tick count immediately before handing control to DOS.
    code.extend((0xB4, 0x01, 0xB9))
    code.extend(struct.pack("<H", ticks >> 16))
    code.extend((0xBA,))
    code.extend(struct.pack("<H", ticks & 0xFFFF))
    code.extend((0xCD, 0x1A))
    code.extend((0xBA,))  # keep resident through report and vector fields
    patches.append((len(code), "keep_paragraphs")); code.extend(b"\0\0")
    code.extend((0xB4, 0x31, 0xCD, 0x21))  # AH=31h: stay resident

    labels["handler"] = len(code)
    code.extend((0x80, 0xFC, 0x02))      # cmp ah, 02h (get time)
    branch((0x74,), "rtc_time")
    code.extend((0x80, 0xFC, 0x04))      # cmp ah, 04h (get date)
    branch((0x74,), "rtc_date")
    absolute(b"\x2E\xFF\x2E", "old_vector_offset")  # far-jump old BIOS
    labels["rtc_time"] = len(code)
    # DOSBox INT 21h/AH=2Ch exposes its advancing BDA clock in binary form;
    # convert the time fields back to the BCD expected by INT 1Ah/AH=02h.
    code.extend((0x50, 0xB4, 0x2C, 0xCD, 0x21))  # push AX; get DOS time
    for load, store in ((0xC5, 0xC5), (0xC1, 0xC1), (0xC6, 0xC6)):
        code.extend((0x8A, load, 0xD4, 0x0A, 0xC0, 0xE4, 0x04,
                     0x08, 0xE0, 0x88, store))  # AAM 10; pack BCD
    code.extend((0xB2, 0, 0x58, 0xF8, 0xCF))  # no DST; restore AX; clear CF; IRET
    labels["rtc_date"] = len(code)
    code.extend((0xB5, rtc_values[0], 0xB1, rtc_values[1],
                 0xB6, rtc_values[2], 0xB2, rtc_values[3],
                 0xB0, weekday, 0xF8, 0xCF))

    data_offset = 0x100 + len(code)
    for at, name in patches:
        if name in fields:
            target = data_offset + fields[name]
        elif name == "filename":
            target = data_offset + len(data)
        elif name == "handler":
            target = 0x100 + labels["handler"]
        elif name == "keep_paragraphs":
            target = (0x100 + len(code) + len(data) + len(b"F117CLK.BIN\0") + 15) // 16
        else:
            target = data_offset
        code[at:at + 2] = struct.pack("<H", target)
    for at, label in relative_patches:
        delta = labels[label] - (at + 1)
        if not -128 <= delta <= 127:
            raise ValueError("clock hook branch is out of range")
        code[at] = delta & 0xFF
    return bytes(code + data + b"F117CLK.BIN\0")


def main():
    import win32api, win32con as wc, win32gui, win32process
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--data", required=True)
    parser.add_argument("--out", required=True)
    parser.add_argument("--seconds", type=int, default=90, help="time after flight starts")
    parser.add_argument("--skip-intro", action="store_true", help="skip PLAYER with a normal Space key")
    parser.add_argument("--route", help="front-end route instead of the default")
    parser.add_argument("--frontend", action="store_true", help="stop after route inputs; validate save milestones")
    parser.add_argument("--pilot", choices=("recon",), help="adaptively fly both photos and return on the reference")
    parser.add_argument("--acquisition", choices=("nose", "level"), default="nose",
                        help="normal designation approach used by the recon pilot")
    parser.add_argument("--replay-input", type=Path, help="replay a recorded normal keyboard/mouse input log")
    parser.add_argument("--set-clock-from-log", action="store_true",
                        help="seed DOS date, BIOS ticks and RTC responses from the input header")
    parser.add_argument("--capture-audio", action="store_true",
                        help="capture DOSBox mixer output with Ctrl+F6 while VGAME runs")
    parser.add_argument("--cargo-check", action="store_true", help="apply the independent original-D5 supply-drop checks")
    args = parser.parse_args()
    if args.cargo_check and not args.replay_input:
        parser.error("--cargo-check requires --replay-input")
    if args.set_clock_from_log and not args.replay_input:
        parser.error("--set-clock-from-log requires --replay-input")
    if args.replay_input and (args.route or args.frontend or args.pilot):
        parser.error("--replay-input cannot be combined with --route, --frontend or --pilot")
    replay_ips, replay_time_us, replay_events = (load_input_log(args.replay_input)
        if args.replay_input else (9_000_000, 0, []))
    cargo_validation = None
    if args.cargo_check:
        import cargo_check as cargo_validation
    out = Path(args.out); out.mkdir(parents=True, exist_ok=True)
    game = out / "game"; game.mkdir(exist_ok=True)
    for file in Path(args.data).iterdir():
        if file.is_file() and not file.name.lower().startswith(("gog", "unins", "launch", "support")):
            shutil.copy2(file, game / file.name)
    if args.set_clock_from_log:
        (game / "F117CLK.COM").write_bytes(clock_setter_com(replay_time_us))
    conf = out / "flight.conf"
    clock_command = "f117clk\n" if args.set_clock_from_log else ""
    conf.write_text('[sdl]\nfullscreen=false\noutput=surface\nautolock=false\n[dosbox]\ncaptures=' + str(out.resolve()) + '\n[autoexec]\n@echo off\nmount C "' + str(game.resolve()) + '"\nc:\nkeyb us\ncls\n' + clock_command + 'f117\nexit\n')
    si = subprocess.STARTUPINFO(); si.dwFlags = subprocess.STARTF_USESHOWWINDOW; si.wShowWindow = 7
    proc = subprocess.Popen([str(Path(args.data) / "DOSBOX" / "DOSBox.exe"), "-conf",
        str(Path(args.data) / "dosboxF117A.conf"), "-conf", str(conf.resolve()), "-noconsole"],
        env=dict(os.environ, SDL_VIDEODRIVER="windib"), startupinfo=si, cwd=out.resolve())
    memory = None
    timer_api = None
    if replay_events:
        try:
            timer_api = C.WinDLL("winmm")
            timer_api.timeBeginPeriod.argtypes = [W.UINT]
            timer_api.timeBeginPeriod.restype = W.UINT
            timer_api.timeEndPeriod.argtypes = [W.UINT]
            timer_api.timeEndPeriod.restype = W.UINT
            if timer_api.timeBeginPeriod(1) != 0:
                timer_api = None
        except OSError:
            timer_api = None
    try:
        hwnd = None
        for _ in range(100):
            windows = []
            def callback(h, unused):
                if win32process.GetWindowThreadProcessId(h)[1] == proc.pid and win32gui.IsWindowVisible(h):
                    windows.append(h)
            win32gui.EnumWindows(callback, None)
            if windows:
                hwnd = windows[0]; break
            time.sleep(.05)
        if hwnd is None:
            raise RuntimeError("no DOSBox window")
        time.sleep(1)
        memory = GuestMemory(proc.pid)
        print("guest memory", hex(memory.base), flush=True)

        def key(vk, down):
            scan = win32api.MapVirtualKey(vk, 0)
            win32gui.PostMessage(hwnd, wc.WM_KEYDOWN if down else wc.WM_KEYUP, vk,
                                 1 | scan << 16 | ((1 << 24) if vk in (wc.VK_UP, wc.VK_DOWN, wc.VK_LEFT, wc.VK_RIGHT) else 0)
                                 | (0 if down else 3 << 30))
        def press(vk, hold=.06):
            key(vk, True); time.sleep(hold); key(vk, False)
        def toggle_audio_capture():
            key(wc.VK_CONTROL, True)
            key(wc.VK_F6, True); time.sleep(.1); key(wc.VK_F6, False)
            key(wc.VK_CONTROL, False); time.sleep(.2)
        def click(x, y, move_only=False):
            # DOSBox's non-fullscreen surface is 640x400; normal mouse
            # movement is relative, so measure the client area and use
            # absolute Windows coordinates only for the posted events.
            _, _, width, height = win32gui.GetClientRect(hwnd)
            lp = round(x * width / 320) | round(y * height / 200) << 16
            win32gui.PostMessage(hwnd, wc.WM_MOUSEMOVE, 0, lp); time.sleep(.06)
            if move_only: return
            win32gui.PostMessage(hwnd, wc.WM_LBUTTONDOWN, wc.MK_LBUTTON, lp); time.sleep(.12)
            win32gui.PostMessage(hwnd, wc.WM_LBUTTONUP, 0, lp)

        def replay_key(scan, extended, down):
            vsc = scan | (0xE000 if extended else 0)
            vk = win32api.MapVirtualKey(vsc, 3)  # MAPVK_VSC_TO_VK_EX
            if not vk:
                raise ValueError(f"no Windows key for set-1 scan {vsc:04X}")
            flags = 1 | (scan << 16) | ((1 << 24) if extended else 0)
            if not down:
                flags |= 3 << 30
            win32gui.PostMessage(hwnd, wc.WM_KEYDOWN if down else wc.WM_KEYUP, vk, flags)

        def replay_mouse(x, y, buttons):
            nonlocal replay_mouse_down
            _, _, width, height = win32gui.GetClientRect(hwnd)
            lp = round(x * width / 320) | (round(y * height / 200) << 16)
            wparam = wc.MK_LBUTTON if buttons else 0
            win32gui.PostMessage(hwnd, wc.WM_MOUSEMOVE, wparam, lp)
            if bool(buttons) != replay_mouse_down:
                win32gui.PostMessage(hwnd, wc.WM_LBUTTONDOWN if buttons else wc.WM_LBUTTONUP,
                                     wparam, lp)
                replay_mouse_down = bool(buttons)
        pending = []
        route = [] if args.replay_input else (route_args(args.route) if args.route else base_route())
        for option, value in zip(route[::2], route[1::2]):
            if option not in ("--type", "--click", "--move"): continue
            program, rest = value.split("+", 1); when, content = rest.split(":", 1)
            pending.append((program.removesuffix(".EXE"), int(when) / 9e6, option, content))
        began = time.monotonic(); starts = {}; previous = None; rows = []; sent = set(); writer = None
        skipped_player = False
        frontend_end = None
        flight_end = None
        replay_origin = None
        replay_position = 0
        replay_mouse_down = False
        audio_capture_active = False
        audio_capture_started = False
        last_sample = 0.0
        replay_error = None
        queued_keys = []
        pilot_tick = 0
        pilot_initialized = False
        approach = False
        last_pilot = 0
        flight_block = None
        candidate_error = None
        class ReferenceInput:
            ips = replay_ips
            @property
            def clock(self): return int((time.monotonic() - began) * self.ips)
            @property
            def psp(self): return memory.psp
            def read8(self, address): return memory.read8(address)
            def read16(self, address): return memory.read16(address)
            def read32(self, address): return memory.read32(address)
            def type(self, at, text, hold_ms=60, gap_ms=60):
                special = {r"\D": wc.VK_DOWN, r"\U": wc.VK_UP, r"\L": wc.VK_LEFT,
                           r"\R": wc.VK_RIGHT, r"\2": wc.VK_F2, r"\s": wc.VK_SPACE, r"\r": wc.VK_RETURN}
                shifted = text in ("+", "_")
                vk = special.get(text, {"+": 0xBB, "=": 0xBB, "-": 0xBD, "_": 0xBD, "/": 0xBF}.get(text, ord(text.upper()) if len(text) == 1 else 0))
                if not vk: raise ValueError("unsupported pilot key " + text)
                deadline = began + at / self.ips
                if shifted: queued_keys.append((deadline, wc.VK_SHIFT, True))
                queued_keys.append((deadline, vk, True))
                queued_keys.append((deadline + hold_ms / 1000, vk, False))
                if shifted: queued_keys.append((deadline + hold_ms / 1000, wc.VK_SHIFT, False))
        reference_input = ReferenceInput()
        with (out / "flight.csv").open("w", newline="") as stream:
            time_limit = 600 + (args.seconds if args.pilot else 0)
            if replay_events:
                time_limit = max(time_limit, replay_events[-1][0] / replay_ips + 45)
            while proc.poll() is None and time.monotonic() - began < time_limit:
                now = time.monotonic()
                ready = sorted((event for event in queued_keys if event[0] <= now), key=lambda event: event[0])
                for event in ready:
                    key(event[1], event[2]); queued_keys.remove(event)
                title = win32gui.GetWindowText(hwnd)
                program = title.split("Program:")[-1].strip().upper()
                for suffix in (".EXE", ".COM"):
                    if program.endswith(suffix):
                        program = program[:-len(suffix)]
                if program != previous:
                    print(round(now - began, 2), title, flush=True)
                    starts.setdefault(program, now); previous = program
                    if args.capture_audio and audio_capture_active and program != "VGAME":
                        toggle_audio_capture()
                        audio_capture_active = False
                        print("stopped DOSBox WAV capture", flush=True)
                    if args.capture_audio and not audio_capture_active and program == "VGAME":
                        toggle_audio_capture()
                        audio_capture_active = audio_capture_started = True
                        print("started DOSBox WAV capture", flush=True)
                    if args.replay_input and replay_origin is None and program == "SETUP":
                        replay_origin = now
                        ticks = int.from_bytes(memory.read(memory.base + 0x46C, 4), "little")
                        print("replay origin SETUP; BIOS ticks", ticks, flush=True)
                if args.replay_input and replay_origin is None and now - began > 30:
                    replay_error = "SETUP program never appeared for the input replay"
                    break
                if replay_origin is not None:
                    while replay_position < len(replay_events):
                        event = replay_events[replay_position]
                        if replay_origin + event[0] / replay_ips > now:
                            break
                        if event[1] == "K":
                            code = event[2]
                            replay_key(code & 0x7F, event[3], not bool(code & 0x80))
                        else:
                            replay_mouse(event[2], event[3], event[4])
                        replay_position += 1
                if args.skip_intro and program == "PLAYER" and not skipped_player and now - starts[program] > 1:
                    press(wc.VK_SPACE); skipped_player = True
                for item in pending[:]:
                    name, when, option, content = item
                    if name == program and now - starts[name] >= max(when, .5):
                        print("input", item, flush=True)
                        if option in ("--click", "--move"): click(*map(int, content.split(",")), move_only=option == "--move")
                        elif option == "--type":
                            position = 0
                            while position < len(content):
                                text = content[position]; position += 1
                                if text == "\\":
                                    escape = content[position]; position += 1
                                    press({"r": wc.VK_RETURN, "e": wc.VK_ESCAPE, "b": wc.VK_BACK}[escape]); continue
                                if text.isupper(): key(wc.VK_SHIFT, True)
                                press(ord(text.upper()))
                                if text.isupper(): key(wc.VK_SHIFT, False)
                        pending.remove(item)
                if args.frontend and not pending:
                    if frontend_end is None: frontend_end = now
                    if now - frontend_end > 3: break
                if program == "VGAME":
                    elapsed = now - starts[program]
                    for when, vk, hold in ([] if args.pilot or args.replay_input else [(11.11, 0xBB, .06), (18.89, wc.VK_DOWN, 1.0),
                                           (30, ord("6"), .06), (40, wc.VK_F2, .06),
                                           (50, ord("8"), .06), (55, wc.VK_SPACE, .06), (60, wc.VK_RETURN, .06)]):
                        if elapsed >= when and when not in sent:
                            if vk == 0xBB: key(wc.VK_SHIFT, True)
                            press(vk, hold)
                            if vk == 0xBB: key(wc.VK_SHIFT, False)
                            sent.add(when)
                    sample_due = args.pilot or not args.replay_input or now - last_sample >= .1
                    state = memory.refresh() if sample_due else None
                    if state is not None:
                        last_sample = now
                    if state is not None and args.cargo_check:
                        state = cargo_validation.observe(reference_input)
                        state["clock"] = reference_input.clock
                        ds = (memory.psp + 0x10 + 0x1E42) << 4
                        state["bios_ticks"] = memory.read32(0x46C)
                        state["rng"] = memory.read16(ds + 0x929E)
                    if args.pilot and state and state["x"] and elapsed > 3.4:
                        from recon_pilot import recon_state, control as recon_control
                        import math
                        state = recon_state(reference_input)
                        state["bios_ticks"] = memory.read32(0x46C)
                        flight_block = state["flight_block"]
                        if not pilot_initialized:
                            if state["objective_type"] != 1 or state["secondary_type"] != 1:
                                candidate_error = "generated mission does not have two reconnaissance objectives"
                                rows.append({"seconds": elapsed, **state})
                                print(candidate_error, rows[-1], flush=True)
                                break
                            flight_clock = int((starts[program] - began) * reference_input.ips)
                            if state["flags"] & 8: reference_input.type(flight_clock + 80000000, "0")
                            reference_input.type(flight_clock + 100000000, "+")
                            reference_input.type(flight_clock + 170000000, r"\D", hold_ms=1000)
                            pilot_initialized = True
                        if elapsed > 21.12 and now - last_pilot >= .2:
                            if state["flags"] & 0x6000 == 0x6000:
                                if math.hypot(signed(state["home_x"] - state["x"]), signed(state["home_y"] + 4000 - state["y"])) < 150:
                                    approach = True
                                landing_control(reference_input, state, pilot_tick, approach)
                            elif state["flags"] & 0x4000:
                                target = ((memory.psp + 0x10 + 0x1E42) << 4) + 0xB2CE + state["secondary_target"] * 16
                                working = {**state, "target": state["secondary_target"], "photos": 0,
                                    "target_x": memory.read16(target + 2), "target_y": memory.read16(target + 4), "cue": state["cue"] >> 1}
                                working["target_range"] = math.hypot(signed(working["target_x"] - state["x"]), signed(working["target_y"] - state["y"]))
                                recon_control(reference_input, working, pilot_tick, acquisition=args.acquisition)
                                if pilot_tick % 10 == 5 and working["target_range"] < 1500 and state["lock"] != 0xFFFF and state["lock"] & 0x7F != state["secondary_target"]:
                                    reference_input.type(reference_input.clock + 1, "b", hold_ms=20)
                            else: recon_control(reference_input, state, pilot_tick, acquisition=args.acquisition)
                            last_pilot = now; pilot_tick += 1
                    if state and 1 <= state["S"] <= 15 and state["home"] < 256 and (not args.pilot or pilot_initialized):
                        row = {"seconds": elapsed, **state}; rows.append(row)
                        if writer is None:
                            writer = csv.DictWriter(stream, fieldnames=row.keys()); writer.writeheader()
                        writer.writerow(row); stream.flush()
                        if len(rows) % 50 == 1: print(row, flush=True)
                    if not args.replay_input and elapsed >= args.seconds: break
                    if (replay_events and replay_position == len(replay_events)
                            and program == "VGAME"
                            and now >= replay_origin + replay_events[-1][0] / replay_ips + 2):
                        break
                elif args.pilot and rows:
                    # VGAME exits through DSWAP before END is loaded. Keep the
                    # final flight observation and wait for the actual debrief,
                    # instead of treating the transient swapper as a failure.
                    if flight_end is None: flight_end = now
                    if program == "END" or now - flight_end > 30: break
                time.sleep(.005 if args.replay_input else .01 if args.pilot else .1)
        if audio_capture_active:
            toggle_audio_capture()
            audio_capture_active = False
            print("stopped DOSBox WAV capture", flush=True)
        errors = [candidate_error] if candidate_error else []
        if replay_error:
            errors.append(replay_error)
        if args.replay_input and replay_position != len(replay_events):
            errors.append(f"input replay sent {replay_position} of {len(replay_events)} events")
        if args.frontend:
            if pending: errors.append("not all front-end inputs were sent")
            errors.extend(check_route(args.route, "", game))
        elif args.cargo_check:
            errors.extend(cargo_validation.errors(rows))
            if program != "VGAME": errors.append("DOSBox left VGAME before the cargo observation finished")
        elif not rows: errors.append("flight state was never observed")
        elif not any(row["agl"] > row["ground"] + 100 for row in rows): errors.append("no airborne flight")
        report = {}
        if args.pilot:
            from recon_pilot import recon_errors
            from landing_pilot import landing_errors
            memory.ram = memory.read(memory.base, 0x100000)
            if flight_block:
                report = {"mission_result": memory.read16(flight_block + 0x28), "pilot_status": memory.read16(flight_block + 0x26)}
            errors.extend(recon_errors(rows, complete=True))
            # The process observer cannot assert the DOS exit code. It
            # independently requires END plus the actual result block.
            if program != "END": errors.append("reference did not enter END")
            errors.extend(landing_errors(rows, report, "", require_dos_exit=False))
        clock_seed = None
        if args.set_clock_from_log:
            clock_path = game / "F117CLK.BIN"
            if not clock_path.is_file() or clock_path.stat().st_size != 20:
                errors.append("DOSBox clock seeder did not write its 20-byte report")
            else:
                raw = clock_path.read_bytes()
                year = struct.unpack_from("<H", raw, 1)[0]
                actual = (year, raw[3], raw[4], raw[5], raw[6], raw[7], raw[8])
                expected = datetime.fromtimestamp(replay_time_us / 1_000_000, timezone.utc)
                expected_cs = (replay_time_us // 10_000) % (24 * 60 * 60 * 100)
                actual_cs = ((raw[5] * 3600 + raw[6] * 60 + raw[7]) * 100 + raw[8])
                delta_cs = min((actual_cs - expected_cs) % (24 * 60 * 60 * 100),
                               (expected_cs - actual_cs) % (24 * 60 * 60 * 100))
                clock_seed = {"date_status": raw[0], "date": f"{year:04d}-{raw[3]:02d}-{raw[4]:02d}",
                    "time": f"{raw[5]:02d}:{raw[6]:02d}:{raw[7]:02d}.{raw[8]:02d}",
                    "expected": expected.strftime("%Y-%m-%d %H:%M:%S"),
                    "time_delta_centiseconds": delta_cs,
                    "bda_ticks": struct.unpack_from("<I", raw, 9)[0],
                    "rtc_bcd": {"date": [raw[13], raw[14], raw[15], raw[16]],
                        "time": [raw[17], raw[18], raw[19]]}}
                rtc_expected = bytes(((value // 10) << 4) | (value % 10) for value in (
                    expected.year // 100, expected.year % 100, expected.month,
                    expected.day, expected.hour, expected.minute, expected.second))
                clock_seed["rtc_matches_header"] = raw[13:20] == rtc_expected
                if raw[0] != 0 or actual[:3] != (expected.year, expected.month, expected.day):
                    errors.append("DOSBox did not accept the recorded guest date")
                if delta_cs > 20:
                    errors.append("DOSBox guest time differs from the input header by more than 200 ms")
                if raw[13:20] != rtc_expected:
                    errors.append("DOSBox INT 1Ah RTC date/time differs from the input header")
        result = {"errors": errors, "samples": len(rows),
            "input_timing": "wall time since program title; not exact instruction replay",
            "final": rows[-1] if rows else None, **report}
        if args.replay_input:
            result["input_replay"] = {"file": str(args.replay_input), "sent": replay_position,
                "total": len(replay_events), "anchored_at_setup": replay_origin is not None}
        if args.capture_audio:
            wavs = sorted(str(path.resolve()) for path in out.rglob("*.wav"))
            result["audio_capture"] = {"started_in_vgame": audio_capture_started,
                "wav_files": wavs}
            if not audio_capture_started:
                errors.append("DOSBox WAV capture did not start in VGAME")
            if not wavs:
                errors.append("DOSBox WAV capture produced no WAV file")
            result["errors"] = errors
        if clock_seed:
            result["guest_clock_seed"] = clock_seed
        (out / "result.json").write_text(json.dumps(result, indent=2) + "\n")
        return int(bool(errors))
    finally:
        if memory: memory.close()
        if timer_api: timer_api.timeEndPeriod(1)
        if proc.poll() is None:
            proc.terminate(); proc.wait(timeout=10)


if __name__ == "__main__":
    raise SystemExit(main())
