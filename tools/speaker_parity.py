#!/usr/bin/env python3
"""speaker_parity.py - the PC speaker held to DOSBox-X, under the game's speaker (IBM) driver.

    py tools/speaker_parity.py --data GOG_DIR [--dosbox-x PATH\\dosbox-x.exe] [--work DIR]
                               [--scene intro|flight|both]

The speaker's input is PIT counter 2 and port 61h. Both scenes run on this
machine (f117run --speaker-log) and on DOSBox-X built with
tools/ref86box/dosbox-x-auto-video.patch (DBX_SPEAKER_LOG logs every write
to ports 42h, 43h for counter 2, and 61h), on copies of the install:

  intro   SETUP answered N, 1; the logo and intro play their music on the
          speaker (counter 2 in mode 3, its count rewritten for vibrato and
          noise). Checked: the gate changes and control words, in order and
          value; the share of counts equal; and the audio, this machine's
          speaker rendered by build/audio_render against DOSBox-X's capture
          (envelope, spectral cosine, level; audio_compare.py).
  flight  boot_to_flight's inputs with SETUP's 1: the takeoff radio call is
          digitised speech (counter 2 in mode 0, a count every carrier period).
          Checked: its counts, in order and value, after the leading silence.

DOSBox-X runs on an invisible desktop with the SDL dummy drivers and its
mixer silenced: its -silent switch would also turn its PC speaker off.
DOSBox-X does not play the radio call (it ignores mode 0 counts written
without a control word), so the speech is compared at the port level only.
The thresholds are at the top of the file and say what was measured.
Exit 0 when every check passes.
"""
import argparse
import difflib
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent
sys.path.insert(0, str(HERE))
import audio_compare  # noqa: E402
import save_parity  # noqa: E402

IPS = 9_000_000
# Measured 8 Oct 2026 over two DOSBox-X runs (intro, 138 s; the radio call of boot_to_flight).
LIMITS = dict(
    intro_min_envelope=0.85,   # 0.872-0.875 (the renderer before the 8254 model: 0.690)
    intro_min_spectral=0.90,   # 0.917-0.926 (before: 0.539)
    intro_level=(0.85, 1.05),  # 0.944-0.946 (before: 0.473)
    intro_min_counts_equal=0.30,  # 0.36-0.57 of 30,044 counts at the same position: the vibrato and noise
                                  # counts follow the timer's phase, which differs between the machines and runs
)


def copy_install(data, game):
    shutil.rmtree(game, ignore_errors=True)
    game.mkdir(parents=True)
    for p in Path(data).iterdir():
        if p.is_file() and not p.name.lower().startswith(("unins", "goggame", "gog", "launch", "support")):
            shutil.copy2(p, game)


def run_hidden(cmd, env, cwd, timeout):
    """Start cmd on its own invisible desktop (nothing reaches the user's screen) and wait."""
    import win32con, win32event, win32process, win32service
    desk = win32service.CreateDesktop("f117speaker", 0, win32con.GENERIC_ALL, None)
    si = win32process.STARTUPINFO()
    si.lpDesktop = "f117speaker"
    hp, _, _, _ = win32process.CreateProcess(None, subprocess.list2cmdline(cmd), None, None, False,
                                             win32process.CREATE_NO_WINDOW,
                                             {str(k): str(v) for k, v in env.items()}, cwd, si)
    try:
        if win32event.WaitForSingleObject(hp, int(timeout * 1000)) != win32event.WAIT_OBJECT_0:
            win32process.TerminateProcess(hp, 1)
    finally:
        desk.CloseDesktop()


def run_dosbox_x(exe, data, out, scene, seconds):
    game, cap = out / "game", out / "capture"
    copy_install(data, game)
    shutil.rmtree(cap, ignore_errors=True)
    cap.mkdir(parents=True)
    env = dict(os.environ, DBX_AUTO_VIDEO="1", DBX_AUTO_LOG=str(out / "auto.log"),
               DBX_SPEAKER_LOG=str(out / "speaker_ports.log"), SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
    lines = ["[sdl]", "fullscreen=false", "output=surface", "[dosbox]", "captures=" + str(cap),
             "[mixer]", "nosound=true", "[cpu]", "turbo=true", "stop turbo on key=false",
             "[autoexec]", "@echo off", 'mount C "%s"' % game, "c:", "keyb us", "cls"]
    if scene == "intro":
        env.update(DBX_AUTO_INPUT_AT="SETUP", DBX_AUTO_INPUT="5000|k|n;5800|k|1")
    else:
        events, _ = save_parity.route_events(HERE / "routes" / "boot_to_flight.args")
        env.update(DBX_AUTO_INPUT_AT="START.EXE", DBX_AUTO_INPUT=events)
        lines.append("autotype -w 5 -p 0.8 n 1")
    lines += ["f117", "exit", ""]
    (out / "dbx.conf").write_text("\n".join(lines))
    for f in ("auto.log", "speaker_ports.log"):
        (out / f).unlink(missing_ok=True)
    run_hidden([str(exe), "-nogui", "-nomenu", "-conf", str(Path(data) / "dosboxF117A.conf"),
                "-conf", str(out / "dbx.conf"), "-time-limit", str(seconds)], env, os.path.dirname(exe), seconds * 3 + 180)
    starts = {}
    for line in open(out / "auto.log"):
        m = re.match(r"([\d.]+) exec (\S+)", line)
        if m:
            starts.setdefault(m.group(2).upper(), float(m.group(1)))
    return starts


def run_ours(data, out, scene):
    out.mkdir(parents=True, exist_ok=True)
    log = out / "speaker.log"
    if scene == "intro":
        (out / "save").mkdir(exist_ok=True)
        r = subprocess.run([str(ROOT / "build" / "f117run.exe"), "--engine", "recomp", "--data", str(data),
                            "--save", str(out / "save"), "--log", str(out / "run.log"),
                            "--type", "SETUP.EXE+200000:N", "--type", "SETUP.EXE+2000000:1",
                            "--steps", str(165 * IPS), "--time-us", "700000000000000", "--speaker-log", str(log)],
                           capture_output=True, text=True)
    else:
        route = out / "boot_to_flight_speaker.args"
        text = (HERE / "routes" / "boot_to_flight.args").read_text()
        route.write_text(text.replace("SETUP.EXE+2000000:2", "SETUP.EXE+2000000:1"))
        r = subprocess.run([sys.executable, str(HERE / "run_route.py"), str(route), "--data", str(data),
                            "--out", str(out), "--", "--speaker-log", str(log)],
                           capture_output=True, text=True)
    (out / "runner.txt").write_text(r.stdout + r.stderr)
    r.check_returncode()
    starts = {}
    for line in open(out / "run.log", errors="replace"):
        m = re.match(r"\[exec\] (\S+).*@(\d+)", line)
        if m:
            starts.setdefault(m.group(1).upper(), int(m.group(2)) * 1000.0 / IPS)
    return log, starts


def ours_events(path, from_ms=0.0):
    """(ms, (kind, value)) from f117run --speaker-log: P port-61h bits, C control word (mode), N count."""
    ev, p61 = [], 0
    for line in open(path):
        f = line.split()
        clk, port61, reload, mode, null = int(f[0]), int(f[1], 16) & 3, int(f[2]), int(f[3]), int(f[5])
        if port61 != p61:
            p61, e = port61, ("P", port61)
        elif null:
            e = ("C", mode)
        else:
            e = ("N", reload or 65536)
        if clk * 1000.0 / IPS >= from_ms:
            ev.append((clk * 1000.0 / IPS, e))
    return ev


def dbx_events(path, from_ms=0.0, until_ms=1e18):
    ev, p61, access, hi_next, lo = [], 0, 3, False, 0
    for line in open(path):
        t, port, v = line.split()
        t, port, v = float(t), int(port, 16), int(v, 16)
        e = None
        if port == 0x43:
            access, hi_next, e = (v >> 4) & 3, False, ("C", (v >> 1) & 7)
        elif port == 0x42:
            if access == 1:
                e = ("N", v or 65536)
            elif access == 2:
                e = ("N", (v << 8) or 65536)
            elif not hi_next:
                lo, hi_next = v, True
            else:
                hi_next, e = False, ("N", (lo | v << 8) or 65536)
        elif port == 0x61 and (v & 3) != p61:
            p61, e = v & 3, ("P", v & 3)
        if e and from_ms <= t <= until_ms:
            ev.append((t, e))
    return ev


def matched(a, b):
    sm = difflib.SequenceMatcher(None, a, b, autojunk=False)
    return sum(m.size for m in sm.get_matching_blocks())


def intro(args, work):
    dbx = run_dosbox_x(args.dosbox_x, args.data, work / "dbx_intro", "intro", 150)
    log, ours = run_ours(args.data, work / "ours_intro", "intro")
    o = ours_events(log, ours["MPS_LOGO.EXE"])
    d = dbx_events(work / "dbx_intro" / "speaker_ports.log", dbx["MPS_LOGO.EXE"], dbx["MPS_LOGO.EXE"] + 150000)
    o = [x for x in o if x[0] - ours["MPS_LOGO.EXE"] <= d[-1][0] - dbx["MPS_LOGO.EXE"]]
    ok = True
    for kind, name in (("P", "port 61h changes"), ("C", "control words")):
        a, b = [e for _, e in o if e[0] == kind], [e for _, e in d if e[0] == kind]
        same = a == b
        ok &= same
        print("intro %-17s here %d, DOSBox-X %d: %s" % (name, len(a), len(b), "identical" if same else
                                                          "%d in order and value" % matched(a, b)))
    a, b = [e[1] for _, e in o if e[0] == "N"], [e[1] for _, e in d if e[0] == "N"]
    share = sum(x == y for x, y in zip(a, b)) / max(1, len(b))
    ok &= share >= LIMITS["intro_min_counts_equal"]
    print("intro counts             here %d, DOSBox-X %d: %.3f at the same position equal (>= %.2f)" % (
        len(a), len(b), share, LIMITS["intro_min_counts_equal"]))
    wav = work / "ours_intro" / "speaker.wav"
    empty = work / "ours_intro" / "no_opl.log"
    empty.write_text("")
    subprocess.run([str(ROOT / "build" / "audio_render.exe"), str(empty), str(wav), str(IPS), str(165 * IPS),
                    "--speaker-log", str(log)], check=True, stdout=subprocess.DEVNULL)
    capture = sorted((work / "dbx_intro" / "capture").glob("mps_logo_*.avi"))[0]
    ref = audio_compare.decode(capture)
    mine = audio_compare.decode(wav)[int(ours["MPS_LOGO.EXE"] / 1000.0 * 11025):]
    r = audio_compare.compare(ref, mine)
    level = r["ours_rms"] / r["reference_rms"]
    good = (r["envelope_correlation"] >= LIMITS["intro_min_envelope"]
            and (r["spectral_cosine_median"] or 0) >= LIMITS["intro_min_spectral"]
            and LIMITS["intro_level"][0] <= level <= LIMITS["intro_level"][1])
    ok &= good
    print("intro audio              envelope %.4f (>= %.2f), spectral %.4f (>= %.2f), level %.3f (%.2f-%.2f), %.0f s: %s" % (
        r["envelope_correlation"], LIMITS["intro_min_envelope"], r["spectral_cosine_median"] or 0,
        LIMITS["intro_min_spectral"], level, *LIMITS["intro_level"], r["aligned_seconds"], "PASS" if good else "FAIL"))
    return ok


def speech(events):
    """The radio call: counts written while counter 2 is in mode 0, and their times."""
    mode, out = None, []
    for t, e in events:
        if e[0] == "C":
            mode = e[1]
        elif e[0] == "N" and mode == 0:
            out.append((t, e[1]))
    return out


def flight(args, work):
    dbx = run_dosbox_x(args.dosbox_x, args.data, work / "dbx_flight", "flight", 430)
    log, ours = run_ours(args.data, work / "ours_flight", "flight")
    if "VGAME.EXE" not in dbx:
        print("flight: DOSBox-X did not reach VGAME")
        return False
    a, b = speech(ours_events(log)), speech(dbx_events(work / "dbx_flight" / "speaker_ports.log"))

    def strip(s):
        i = 0
        while i < len(s) and s[i][1] == 128:
            i += 1
        return i, [v for _, v in s[i:]]
    la, va = strip(a)
    lb, vb = strip(b)
    same = bool(va) and va == vb
    period = lambda s: (s[-1][0] - s[0][0]) / (len(s) - 1) * 1193.182 if len(s) > 1 else 0
    print("flight radio call        here %d counts, DOSBox-X %d (after %d and %d silent ones): %s" % (
        len(va), len(vb), la, lb, "identical" if same else "%d in order and value" % matched(va, vb)))
    print("flight carrier period    here %.3f PIT clocks, DOSBox-X %.3f (the driver writes count 79 to counter 0, mode 2)" % (
        period(a), period(b)))
    print("flight call starts       %.1f ms after VGAME here, %.1f ms on DOSBox-X" % (
        a[0][0] - ours["VGAME.EXE"], b[0][0] - dbx["VGAME.EXE"]) if a and b else "flight: no call")
    return same


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--data", required=True)
    ap.add_argument("--dosbox-x", type=Path, default=Path(r"D:\86box-src\dbx-src\src\dosbox-x.exe"))
    ap.add_argument("--work", type=Path, default=Path.home() / "f117-recomp-local" / "speaker-parity")
    ap.add_argument("--scene", choices=("intro", "flight", "both"), default="both")
    a = ap.parse_args()
    sys.stdout.reconfigure(line_buffering=True)
    a.work.mkdir(parents=True, exist_ok=True)
    ok = True
    if a.scene in ("intro", "both"):
        ok &= intro(a, a.work)
    if a.scene in ("flight", "both"):
        ok &= flight(a, a.work)
    print("speaker parity:", "PASS" if ok else "FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
