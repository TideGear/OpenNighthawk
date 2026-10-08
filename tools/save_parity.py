#!/usr/bin/env python3
"""save_parity.py - a scripted START session's saved data, ours against DOSBox-X.

    py tools/save_parity.py --data GOG_DIR [--route tools/routes/roster_edit.args]
                            [--dosbox-x PATH] [--out DIR]

Plays a route's START inputs (keys, mouse moves and clicks, offsets in
instructions after START.EXE is loaded) on this machine (run_route.py, saves
to a private directory) and on the patched DOSBox-X (tools/ref86box/
dosbox-x-auto-video.patch: DBX_AUTO_INPUT schedules the same inputs in
emulated milliseconds from START's EXEC, no host input), then compares the
files the program saved byte for byte. The default route creates a pilot,
edits and cancels names, erases one and returns to the office: ROSTER.FIL
(802 bytes) is written. Runs headless and silent. Exit 0 when every saved
file is identical.
"""
import argparse
import os
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
IPS = 9_000_000
DBX = r"D:\86box-src\dbx-src\src\dosbox-x.exe"
ESC = {"r": "enter", "b": "bs", "e": "esc"}
SAVED = ("ROSTER.FIL",)


def route_events(route):
    """DBX_AUTO_INPUT text from the START inputs of a route file, and the last event's ms."""
    lines = [l.rstrip("\n") for l in open(route) if not l.startswith("#") and l.strip()]
    items, i, last = [], 0, 0.0
    while i < len(lines):
        opt = lines[i]
        if opt in ("--type", "--click", "--move"):
            at, _, arg = lines[i + 1].partition(":")
            prog, _, off = at.partition("+")
            i += 2
            if prog.upper() != "START.EXE":
                continue                                    # SETUP's answers come from AUTOTYPE
            ms = int(off) * 1000.0 / IPS
            last = max(last, ms)
            if opt == "--type":
                k = 0
                while k < len(arg):
                    if arg[k] == "\\":
                        name, k = ESC[arg[k + 1]], k + 2
                    else:
                        name, k = ("space" if arg[k] == " " else arg[k]), k + 1
                    items.append("%d|k|%s" % (ms, name))
                    ms += 150                               # keys of one --type are 150 ms apart
                    last = max(last, ms)
            else:
                items.append("%d|%s|%s" % (ms, "c" if opt == "--click" else "m", arg))
        else:
            i += 2 if opt.startswith("--") and i + 1 < len(lines) and not lines[i + 1].startswith("--") else 1
    return ";".join(items), last


SCAN = dict(zip("abcdefghijklmnopqrstuvwxyz", [0x1E, 0x30, 0x2E, 0x20, 0x12, 0x21, 0x22, 0x23, 0x17, 0x24, 0x25, 0x26, 0x32,
                                                0x31, 0x18, 0x19, 0x10, 0x13, 0x1F, 0x14, 0x16, 0x2F, 0x11, 0x2D, 0x15, 0x2C]))
SCAN.update(zip("1234567890", range(2, 12)))
SCAN.update(enter=0x1C, bs=0x0E, esc=0x01, space=0x39)
SHIFT = 0x2A
ROSTER_FRAME = 11_061               # displayed frame at which the roster first shows (386DX/33, bare boot)
FPS = 70.086


def route_86box(route, base):
    """(B86_KEYS, B86_MOUSE) for a route: the first START input comes one second after displayed frame
    `base`, the rest at the route's own spacing, in frames at the VGA rate. The 86Box is slower than
    the model, so only the order and the gaps (seconds, not milliseconds) are kept."""
    events, _ = route_events(route)
    items = [e.split("|") for e in events.split(";") if e]
    t0 = float(items[0][0])
    keys, mouse = [], []
    for ms, kind, arg in items:
        f = int(base + FPS * (1 + (float(ms) - t0) / 1000.0))
        if kind == "k":
            up = arg.isupper() and len(arg) == 1
            code = SCAN[arg.lower() if len(arg) == 1 else arg]
            if up:
                keys.append("%d:1:%x" % (f, SHIFT))
            keys.append("%d:1:%x" % (f + 2, code))
            keys.append("%d:0:%x" % (f + 5, code))
            if up:
                keys.append("%d:0:%x" % (f + 7, SHIFT))
        else:
            mouse.append("%d:m:%s" % (f, arg))
            if kind == "c":
                mouse.append("%d:b:1" % (f + 40))
                mouse.append("%d:b:0" % (f + 55))
    return ",".join(keys), ";".join(mouse)


def run_86box(route, out, driver):
    keys, mouse = route_86box(route, ROSTER_FRAME)
    # SETUP's two answers, as the DOSBox-X run types them (N, then 2), long before START
    setup = "3000:1:31,3003:0:31,3300:1:03,3303:0:03"
    last = max(int(x.split(":")[0]) for x in (keys + "," + mouse.replace(";", ",")).split(",") if x)
    r = subprocess.run([sys.executable, str(HERE / "ref86box" / "sav86.py"), str(out), "--mouse-driver", str(driver),
                        "--keys", setup + "," + keys, "--mouse", mouse, "--frames", str(last + 600)],
                       capture_output=True, text=True)
    (out / "sav86.txt").write_text(r.stdout + r.stderr)
    return out


def run_ours(data, route, out):
    out.mkdir(parents=True, exist_ok=True)
    r = subprocess.run([sys.executable, str(HERE / "run_route.py"), str(route), "--data", str(data), "--out", str(out)],
                       capture_output=True, text=True)
    (out / "run_route.txt").write_text(r.stdout + r.stderr)
    return out / "save"


def run_dosbox_x(data, route, out, exe, seconds, turbo=True):
    events, last = route_events(route)
    game = out / "game"
    shutil.rmtree(game, ignore_errors=True)
    game.mkdir(parents=True)
    for p in Path(data).iterdir():
        if p.is_file() and not p.name.lower().startswith(("unins", "goggame", "gog", "launch", "support")):
            shutil.copy2(p, game)
    conf = out / "save.conf"
    conf.write_text("\n".join(["[sdl]", "fullscreen=false", "output=surface", "[dosbox]", "captures=" + str(out / "capture"), "[mixer]", "nosound=true",
                               "[cpu]", "turbo=%s" % str(turbo).lower(), "stop turbo on key=false", "[autoexec]",
                               "@echo off", 'mount C "%s"' % game, "c:", "keyb us", "cls",
                               "autotype -w 5 -p 0.8 n 2", "f117", "exit", ""]))
    env = dict(os.environ, DBX_AUTO_INPUT=events, DBX_AUTO_INPUT_AT="START.EXE")
    roster = game / "ROSTER.FIL"
    before = roster.read_bytes() if roster.exists() else None
    proc = subprocess.Popen([exe, "-silent", "-nogui", "-conf", str(Path(data) / "dosboxF117A.conf"), "-conf", str(conf),
                             "-time-limit", str(seconds)], env=env, cwd=os.path.dirname(exe),
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    # The inputs run on emulated time, so how long the session takes in real time depends on the load:
    # stop when the roster's content has changed from the install's (START also rewrites it unchanged
    # early on), not after a fixed wall time; `seconds` is only the ceiling.
    deadline = time.time() + seconds
    try:
        while proc.poll() is None and time.time() < deadline:
            time.sleep(2)
            if roster.exists() and roster.read_bytes() != before:
                time.sleep(5)                                 # the write is finished and the program idles
                break
    finally:
        if proc.poll() is None:
            proc.kill()
        proc.wait()
    return game


def compare(name, ours, ref_dir, label):
    mine = next((p for p in ours.iterdir() if p.name.upper() == name), None) if ours.exists() else None
    ref = next((p for p in Path(ref_dir).iterdir() if p.name.upper() == name), None) if Path(ref_dir).exists() else None
    if mine is None or ref is None:
        print("save %-10s %-8s ERROR missing (ours %s, reference %s)" % (name, label, bool(mine), bool(ref)))
        return False
    x, y = mine.read_bytes(), ref.read_bytes()
    diff = [i for i in range(min(len(x), len(y))) if x[i] != y[i]]
    same = x == y
    print("save %-10s %-8s %s  ours %d bytes, reference %d bytes, %d bytes differ%s" % (
        name, label, "PASS" if same else "FAIL", len(x), len(y), len(diff) + abs(len(x) - len(y)),
        "" if same else " (first at 0x%X)" % diff[0] if diff else ""))
    return same


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--data", required=True)
    ap.add_argument("--route", default=str(ROOT / "tools" / "routes" / "roster_edit.args"))
    ap.add_argument("--dosbox-x", default=DBX)
    ap.add_argument("--mouse-driver", default=r"D:\f117-gate\ctm\CTMOUSE.EXE", help="CuteMouse ctmouse.exe for the 86Box guest")
    ap.add_argument("--out", type=Path, default=Path.home() / "f117-recomp-local" / "save-parity")
    ap.add_argument("--seconds", type=int, default=0, help="ceiling in wall seconds for DOSBox-X (default: three times intro + route + margin)")
    ap.add_argument("--no-dosbox-x", action="store_true")
    ap.add_argument("--no-86box", action="store_true")
    a = ap.parse_args()
    events, last = route_events(a.route)
    seconds = a.seconds or int(3 * (150 + last / 1000 + 60))      # a ceiling: the run ends when the roster is written
    ours = run_ours(a.data, a.route, a.out / "ours")
    ok = True
    if not a.no_dosbox_x:
        game = run_dosbox_x(a.data, a.route, a.out / "dosbox-x", a.dosbox_x, seconds)
        for name in SAVED:
            ok &= compare(name, ours, game, "DOSBox-X")
    if not a.no_86box:
        box = run_86box(a.route, a.out / "86box", a.mouse_driver)
        for name in SAVED:
            ok &= compare(name, ours, box, "86Box")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
