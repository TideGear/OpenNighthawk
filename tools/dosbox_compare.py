#!/usr/bin/env python3
"""dosbox_compare.py - the game's music, here and in GOG's DOSBox.

    py tools/dosbox_compare.py --data DIR [--seconds 120] [--keep]

The fidelity probe (tools/fidelity.py) compares the machines; this compares
the game running on them. The stretch of the game that needs no input -
SETUP answered (no joystick, AdLib), the MicroProse logo, the intro with its
music, up to the pilot roster - runs under GOG's DOSBox with its raw OPL
capture switched on, and under f117run with --opl-log. Every write to the
AdLib's registers is then compared, in order, with its time.

DOSBox is driven without taking the screen: a minimised window (SDL's windib
driver, so posted keys reach DOSBox's mapper), SETUP's answers redirected
from a file (it reads them through DOS), Ctrl+Alt+F7 posted to start and
stop the capture. It works on a scratch copy of the install and writes its
capture there.

DOSBox's capture (src/hardware/adlib.cpp) records only the registers in its
table (not the timer counts 02h/03h), only writes that change a register,
to the millisecond, and starts with a dump of the registers set so far; the
log from here is filtered the same way before the two are aligned.
"""
from __future__ import annotations

import argparse
import os
import shutil
import struct
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
WORK = os.path.join(os.path.expanduser("~"), "f117-recomp-local", "dbxcompare")
IPS = 9_000_000


def dro_registers():
    """The registers DOSBox's capture records (Capture::Capture), less the
    timer registers 02h-04h: DOSBox's OPL2 handles those in its timer
    emulation (OPL::Chip::Write) and never passes them on to be captured."""
    regs = {0x01, 0x05, 0x08, 0xBD}
    for i in range(0x16):
        if (i & 7) < 6:
            regs |= {0x20 + i, 0x40 + i, 0x60 + i, 0x80 + i, 0xE0 + i}
    for i in range(9):
        regs |= {0xA0 + i, 0xB0 + i, 0xC0 + i}
    return regs


def read_dro(path):
    """[(ms, reg, val)] from a DOSBox 0.74 DRO v2 file."""
    b = open(path, "rb").read()
    if b[:8] != b"DBRAWOPL":
        sys.exit("%s is not a DRO capture" % path)
    (vh, vl, cmds, ms, hw, fmt, comp, d256, dshift, tsize) = struct.unpack_from("<HHIIBBBBBB", b, 8)
    table = b[26:26 + tsize]
    data = b[26 + tsize:]
    out, t = [], 0
    for i in range(0, len(data) - 1, 2):
        c, v = data[i], data[i + 1]
        if c == d256:
            t += v + 1
        elif c == dshift:
            t += (v + 1) << 8
        else:
            out.append((t, table[c & 0x7F] | (0x100 if c & 0x80 else 0), v))
    return out


def filter_log(path):
    """[(ms, reg, val, icount)] from an f117run --opl-log, as DOSBox would
    have recorded it: mapped registers only, changes only."""
    regs, cache, out = dro_registers(), [0] * 256, []
    for line in open(path):
        ic, r, v = line.split()
        ic, r, v = int(ic), int(r, 16), int(v, 16)
        old, cache[r] = cache[r], v
        if r in regs and old != v:
            out.append((ic * 1000 // IPS, r, v, ic))
    return out


def run_dosbox(data, game, seconds):
    import win32api, win32con, win32gui, win32process
    cap = os.path.join(WORK, "capture")
    shutil.rmtree(cap, ignore_errors=True)
    os.makedirs(cap)
    conf = os.path.join(WORK, "compare.conf")
    with open(conf, "w") as f:
        f.write("[sdl]\nfullscreen=false\noutput=surface\n[dosbox]\ncaptures=%s\n[autoexec]\n@echo off\n" % cap)
        f.write('mount C "%s"\nc:\nkeyb us\ncls\nf117\nexit\n' % game)
    dosbox = os.path.join(data, "DOSBOX", "DOSBox.exe")
    si = subprocess.STARTUPINFO()
    si.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    si.wShowWindow = 7                                   # minimised, not activated
    env = dict(os.environ, SDL_VIDEODRIVER="windib")
    proc = subprocess.Popen([dosbox, "-conf", os.path.join(data, "dosboxF117A.conf"), "-conf", conf, "-noconsole"],
                            cwd=os.path.dirname(dosbox), startupinfo=si, env=env)
    hwnd = None
    for _ in range(100):
        found = []

        def cb(h, _):
            _, pid = win32process.GetWindowThreadProcessId(h)
            if pid == proc.pid and win32gui.IsWindowVisible(h):
                found.append(h)
        win32gui.EnumWindows(cb, None)
        if found:
            hwnd = found[0]
            break
        time.sleep(0.05)
    if not hwnd:
        proc.kill()
        sys.exit("no DOSBox window")

    def key(vk, down, alt):
        sc = win32api.MapVirtualKey(vk, 0)
        lp = 1 | (sc << 16) | ((1 << 29) if alt else 0) | (0 if down else (3 << 30))
        msg = (win32con.WM_SYSKEYDOWN if down else win32con.WM_SYSKEYUP) if alt else \
              (win32con.WM_KEYDOWN if down else win32con.WM_KEYUP)
        win32api.PostMessage(hwnd, msg, vk, lp)

    def ctrl_alt_f7():
        key(win32con.VK_CONTROL, True, False); key(win32con.VK_MENU, True, True); key(win32con.VK_F7, True, True)
        time.sleep(0.1)
        key(win32con.VK_F7, False, True); key(win32con.VK_MENU, False, True); key(win32con.VK_CONTROL, False, False)

    def press(vk):
        key(vk, True, False); time.sleep(0.08); key(vk, False, False)

    # SETUP's questions, answered at the keyboard as a player would (a
    # redirected stdin looks like a waiting key to the intro, which then
    # skips itself): no joystick, AdLib.
    for _ in range(200):
        if "SETUP" in win32gui.GetWindowText(hwnd):
            break
        time.sleep(0.05)
    time.sleep(0.5)
    press(ord("N")); time.sleep(0.5); press(ord("2"))
    # The mapper takes keys only once DOSBox is running: press until the
    # capture file appears.
    for _ in range(40):
        ctrl_alt_f7()
        time.sleep(0.5)
        if os.listdir(cap):
            break
    else:
        proc.kill()
        sys.exit("DOSBox did not start the capture")
    # what DOSBox runs, from its title bar ("Program: NAME"), as it changes
    t0, last = time.time(), None
    while time.time() - t0 < seconds:
        title = win32gui.GetWindowText(hwnd)
        prog = title.split("Program:")[-1].strip() if "Program:" in title else title
        if prog != last:
            print("  DOSBox %5.1f s: %s" % (time.time() - t0, prog), flush=True)
            last = prog
        time.sleep(0.25)
    if not win32gui.IsWindow(hwnd):
        sys.exit("DOSBox closed before the end of the capture")
    ctrl_alt_f7()                                       # stop: the file is completed
    time.sleep(1.0)
    win32api.PostMessage(hwnd, win32con.WM_CLOSE, 0, 0)  # and DOSBox closes cleanly
    try:
        proc.wait(timeout=15)
    except subprocess.TimeoutExpired:
        proc.kill()
    files = sorted(os.listdir(cap))
    if not files:
        sys.exit("DOSBox wrote no capture")
    return [os.path.join(cap, f) for f in files]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--data", required=True, help="GOG install (holds DOSBOX and dosboxF117A.conf)")
    ap.add_argument("--seconds", type=int, default=120)
    ap.add_argument("--window", type=int, default=40, help="writes that must match to align")
    a = ap.parse_args()
    os.makedirs(WORK, exist_ok=True)
    game = os.path.join(WORK, "game")
    shutil.rmtree(game, ignore_errors=True)
    os.makedirs(game)
    for f in os.listdir(a.data):
        p = os.path.join(a.data, f)
        if os.path.isfile(p) and not f.lower().startswith(("unins", "goggame", "gog", "launch", "support")):
            shutil.copy2(p, game)

    print("DOSBox: %d s with the raw OPL capture on" % a.seconds, flush=True)
    dros = run_dosbox(a.data, game, a.seconds)
    ref = []
    for d in dros:
        ref += read_dro(d)
    print("  %d writes captured in %s" % (len(ref), ", ".join(os.path.basename(d) for d in dros)))

    log = os.path.join(WORK, "opl.log")
    steps = (a.seconds + 15) * IPS
    print("f117run: %d instructions with --opl-log" % steps, flush=True)
    subprocess.run([os.path.join(ROOT, "build", "f117run.exe"), "--engine", "recomp", "--data", game,
                    "--save", os.path.join(WORK, "save"), "--log", os.path.join(WORK, "run.log"),
                    "--type", "SETUP.EXE+200000:N", "--type", "SETUP.EXE+2000000:2",
                    "--steps", str(steps), "--time-us", "700000000000000", "--opl-log", log],
                   capture_output=True, text=True)
    ours = filter_log(log)
    print("  %d writes (as DOSBox records them)" % len(ours))

    # Align: the capture begins with a dump of the registers already set; find
    # where a window of its live writes first appears in ours.
    W = a.window
    pairs_ours = [(r, v) for _, r, v, _ in ours]
    start_ref = start_ours = None
    for i in range(0, min(len(ref) - W, 2000)):
        win = [(r, v) for _, r, v in ref[i:i + W]]
        for j in range(len(pairs_ours) - W):
            if pairs_ours[j:j + W] == win:
                start_ref, start_ours = i, j
                break
        if start_ref is not None:
            break
    if start_ref is None:
        sys.exit("could not align the two sequences (no %d-write window in common)" % W)
    n = min(len(ref) - start_ref, len(ours) - start_ours)
    mismatch = next((k for k in range(n) if (ref[start_ref + k][1], ref[start_ref + k][2]) !=
                     (ours[start_ours + k][1], ours[start_ours + k][2])), None)
    same = n if mismatch is None else mismatch
    t0r, t0o = ref[start_ref][0], ours[start_ours][0]
    drift = [(ref[start_ref + k][0] - t0r) - (ours[start_ours + k][0] - t0o) for k in range(same)]
    span = ref[start_ref + same - 1][0] - t0r if same else 0
    print("aligned at capture write %d / our write %d" % (start_ref, start_ours))
    print("%d writes in the same order with the same values, over %.1f s of music" % (same, span / 1000.0))
    if drift:
        print("timing, DOSBox minus here, in ms: at the end %+d, smallest %+d, largest %+d" % (
            drift[-1], min(drift), max(drift)))
    if mismatch is not None:
        k = mismatch
        print("FIRST DIFFERENCE at aligned write %d:" % k)
        for d in range(max(0, k - 3), min(n, k + 4)):
            rr, oo = ref[start_ref + d], ours[start_ours + d]
            print("   %s %6d ms %03X=%02X   |   %6d ms %03X=%02X" % (
                ">>" if d == k else "  ", rr[0] - t0r, rr[1], rr[2], oo[0] - t0o, oo[1], oo[2]))
    else:
        print("no difference until one sequence ended (DOSBox %d more, here %d more)" % (
            len(ref) - start_ref - n, len(ours) - start_ours - n))


if __name__ == "__main__":
    main()
