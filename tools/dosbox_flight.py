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
    args = parser.parse_args()
    out = Path(args.out); out.mkdir(parents=True, exist_ok=True)
    game = out / "game"; game.mkdir(exist_ok=True)
    for file in Path(args.data).iterdir():
        if file.is_file() and not file.name.lower().startswith(("gog", "unins", "launch", "support")):
            shutil.copy2(file, game / file.name)
    conf = out / "flight.conf"
    conf.write_text('[sdl]\nfullscreen=false\noutput=surface\nautolock=false\n[dosbox]\ncaptures=' + str(out.resolve()) + '\n[autoexec]\n@echo off\nmount C "' + str(game.resolve()) + '"\nc:\nkeyb us\ncls\nf117\nexit\n')
    si = subprocess.STARTUPINFO(); si.dwFlags = subprocess.STARTF_USESHOWWINDOW; si.wShowWindow = 7
    proc = subprocess.Popen([str(Path(args.data) / "DOSBOX" / "DOSBox.exe"), "-conf",
        str(Path(args.data) / "dosboxF117A.conf"), "-conf", str(conf.resolve()), "-noconsole"],
        env=dict(os.environ, SDL_VIDEODRIVER="windib"), startupinfo=si, cwd=out.resolve())
    memory = None
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
        pending = []
        route = route_args(args.route) if args.route else base_route()
        for option, value in zip(route[::2], route[1::2]):
            if option not in ("--type", "--click", "--move"): continue
            program, rest = value.split("+", 1); when, content = rest.split(":", 1)
            pending.append((program.removesuffix(".EXE"), int(when) / 9e6, option, content))
        began = time.monotonic(); starts = {}; previous = None; rows = []; sent = set(); writer = None
        skipped_player = False
        frontend_end = None
        flight_end = None
        queued_keys = []
        pilot_tick = 0
        pilot_initialized = False
        approach = False
        last_pilot = 0
        flight_block = None
        candidate_error = None
        class ReferenceInput:
            ips = 9000000
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
            while proc.poll() is None and time.monotonic() - began < 600 + (args.seconds if args.pilot else 0):
                now = time.monotonic()
                ready = sorted((event for event in queued_keys if event[0] <= now), key=lambda event: event[0])
                for event in ready:
                    key(event[1], event[2]); queued_keys.remove(event)
                title = win32gui.GetWindowText(hwnd)
                program = title.split("Program:")[-1].strip().upper()
                if program != previous:
                    print(round(now - began, 2), title, flush=True)
                    starts.setdefault(program, now); previous = program
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
                    for when, vk, hold in ([] if args.pilot else [(11.11, 0xBB, .06), (18.89, wc.VK_DOWN, 1.0),
                                           (30, ord("6"), .06), (40, wc.VK_F2, .06),
                                           (50, ord("8"), .06), (55, wc.VK_SPACE, .06), (60, wc.VK_RETURN, .06)]):
                        if elapsed >= when and when not in sent:
                            if vk == 0xBB: key(wc.VK_SHIFT, True)
                            press(vk, hold)
                            if vk == 0xBB: key(wc.VK_SHIFT, False)
                            sent.add(when)
                    state = memory.refresh()
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
                    if elapsed >= args.seconds: break
                elif args.pilot and rows:
                    # VGAME exits through DSWAP before END is loaded. Keep the
                    # final flight observation and wait for the actual debrief,
                    # instead of treating the transient swapper as a failure.
                    if flight_end is None: flight_end = now
                    if program == "END" or now - flight_end > 30: break
                time.sleep(.01 if args.pilot else .1)
        errors = [candidate_error] if candidate_error else []
        if args.frontend:
            if pending: errors.append("not all front-end inputs were sent")
            errors.extend(check_route(args.route, "", game))
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
        (out / "result.json").write_text(json.dumps({"errors": errors, "samples": len(rows),
            "input_timing": "wall time since program title; not exact instruction replay",
            "final": rows[-1] if rows else None, **report}, indent=2) + "\n")
        return int(bool(errors))
    finally:
        if memory: memory.close()
        if proc.poll() is None:
            proc.terminate(); proc.wait(timeout=10)


if __name__ == "__main__":
    raise SystemExit(main())
