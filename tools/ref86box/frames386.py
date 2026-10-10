#!/usr/bin/env python3
"""frames386.py - the intro frame by frame: this machine under --timing 386 against 86Box's 386DX/33.

    py tools/ref86box/frames386.py OUT [--seconds 130]                 run both, then compare
    py tools/ref86box/frames386.py OUT --box DIR --ours DIR             compare earlier runs
    py tools/ref86box/frames386.py OUT --box DIR                        reuse 86Box's run, run ours
    py tools/ref86box/frames386.py OUT --box DIR --ours-mouse           ours with its INT 33h driver

The instrument for the 386DX/33 timing profile (timing386.md, "Frame comparison"): the profile's
goal is that every picture lands on the frame it lands on in 86Box. Both runs answer SETUP (n, then
2 300 frames later) and capture every displayed picture change with its emulated time:

  86Box     the traced build (trace_86box.ps1, build_86box.md) on a copy of D:\\86box\\vmt386dos500, booted
            with probe86.bare_boot in fast-forward (the MS-DOS VM loads its MOUSE.COM; on the
            FreeDOS --profile D:\\86box\\vmt386 no driver unless --mouse-driver
            CTMOUSE.EXE); frames.csv (each displayed frame's TSC, hash
            and instructions) and a PPM of each new picture. Time is 86Box's TSC over 33,333,333.
            86Box posts no frame while the screen is off; a gap of more than 1.5 frame periods is
            read as a blank picture from the first missing frame.
  ours      f117run --timing 386 --engine interp on a copy of the install, sampled once per VGA
            frame (--shots-vga) keeping only changed pictures (--shots-changed), with --frame-log
            (every frame's scan-out clock, steps and screen-off bit) and --record; a picture's
            time is the clock its scan-out completed at over 33,333,333. Ours keeps its INT 33h
            driver when 86Box's run had one; otherwise INT 33h answers as with no driver
            (--no-mouse), as on that VM; --ours-mouse keeps the driver regardless.

Both lists are reduced to mode-13h pictures in 6-bit DAC values (86Box expands the DAC as
floor(v*255/63), this machine as v<<2|v>>4; v>>2 recovers v from both) plus blank periods, and
aligned in order on exact equality (difflib, no tolerance). The origin is the first picture both
show after SETUP's last key. For every 86Box picture change pairs.csv says whether an exact copy
appears in the same place in our sequence and, if so, the drift: 86Box's time since the origin
minus ours, in ms and in frames (475,610 cycles, 70.086 Hz). The report lists:

  scene 0   SETUP's last key to the origin (SETUP's exit, the EXEC of MPS_LOGO, the BIOS mode set,
            the logo file's open), measured on both and kept out of the drift
  steps     consecutive exact pictures between which the drift moved two frames or more, with the
            86Box instructions and our steps in between and the events of our log in that interval
            (EXECs, overlays, file opens); one-frame moves are counted apart, since animations
            alternate 2- and 3-frame holds whose phase can differ by a frame
  scenes    from each picture held a second or more to the next: pictures, exact, the drift's
            first, median and last value and what the scene added (median over median)

The verdict is PASS only when every picture of 86Box's span is matched exactly, in order, and
every matched picture is on the same frame (|drift| under half a frame): that is the profile's
stated goal, not a tolerance. Otherwise FAIL with the numbers. Outputs: OUT/report.json and
OUT/pairs.csv. --only box|ours makes one run (two can go side by side) without comparing.
"""
import argparse
import csv
import difflib
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
import probe86  # noqa: E402

CLOCK = 33_333_333                       # both machines' cycles a second (MACHINE_386_IPS)
FRAME = CLOCK * 359200 / 25175000        # mode 13h: 475,610 cycles, 70.086 Hz
SETUP_KEYS = "3000:1:31,3003:0:31,3300:1:03,3303:0:03"   # N then 2, as capture_intro.py and sound86.py
SETUP_FRAME = 3300                       # the frame 86Box presses 2 at
HELD = 1.0                               # a scene's opening picture stays on screen this long (compare_timing86.py)


# ---- the runs -------------------------------------------------------------------------------

def run_box(out, profile, seconds, mouse_driver=None):
    """The traced 86Box run: OUT/trace/frames.csv and a PPM per picture change from SETUP on."""
    work = os.path.normpath(os.path.join(out, "profile"))
    shutil.rmtree(work, ignore_errors=True)
    os.makedirs(work)
    cfg = open(os.path.join(profile, "86box.cfg")).read()
    if mouse_driver:
        cfg = re.sub(r"(?m)^mouse_type\s*=.*$", "mouse_type = msserial", cfg)
    open(os.path.join(work, "86box.cfg"), "w").write(cfg)
    shutil.copytree(os.path.join(profile, "nvr"), os.path.join(work, "nvr"))
    subprocess.run(["cmd", "/c", "mklink", "/J", os.path.join(work, "roms"), r"D:\86box\app\roms"],
                   stdout=subprocess.DEVNULL, check=True)
    img = os.path.join(work, "f117a.img")
    shutil.copyfile(os.path.join(profile, "f117a.img"), img)

    def install(fs):
        tail = ["F117"]
        if mouse_driver:
            fs.writebytes("/F117A/CTMOUSE.EXE", open(mouse_driver, "rb").read())
            tail = ["CTMOUSE", "F117"]
        probe86.bare_boot(fs, tail)
        # an MS-DOS VM loads its own driver (MOUSE.COM) from AUTOEXEC.BAT
        return "MOUSE.COM" if fs.exists("/MOUSE/MOUSE.COM") and not fs.exists("/FDAUTO.BAT") else None
    mouse_driver = probe86.with_partition(img, install, write=True) or mouse_driver
    trace = os.path.join(out, "trace")
    shutil.rmtree(trace, ignore_errors=True)
    env = dict(os.environ, B86_FAST="1", B86_PPM_AFTER=str(SETUP_FRAME - 100))
    stop = SETUP_FRAME + (seconds + 5) * 72
    subprocess.run(["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File",
                    os.path.join(HERE, "trace_86box.ps1"), "-Profile", work, "-Out", trace, "-Stop", str(stop),
                    "-Keys", SETUP_KEYS, "-Ppm", "-TimeoutSeconds", "3600"], check=True, env=env)
    json.dump(dict(profile=profile, seconds=seconds, mouse_driver=mouse_driver, keys=SETUP_KEYS),
              open(os.path.join(out, "settings.json"), "w"), indent=1)


def run_ours(out, data, exe, seconds, no_mouse=False, opl=False):
    """f117run --timing 386: OUT/shots (changed pictures), OUT/frames.csv, OUT/input.log."""
    root = os.path.realpath(out)
    for name in ("game", "shots", "save"):
        if os.path.dirname(os.path.realpath(os.path.join(out, name))) != root:
            raise ValueError("capture cleanup target leaves its output directory")
    game = os.path.join(out, "game")
    shutil.rmtree(game, ignore_errors=True)
    os.makedirs(game)
    for name in os.listdir(data):                    # a copy per run: the install is never written
        p = os.path.join(data, name)
        if os.path.isfile(p) and not name.lower().startswith(("unins", "goggame", "gog", "launch", "support")):
            shutil.copy2(p, game)
    shots = os.path.join(out, "shots")
    shutil.rmtree(shots, ignore_errors=True)
    os.makedirs(shots)
    save = os.path.join(out, "save")
    shutil.rmtree(save, ignore_errors=True)
    # SETUP answered as on 86Box: N, then 2 300 frames later, each held 3 frames (43 ms)
    n_at, two_at = CLOCK, CLOCK + int(300 * FRAME)
    steps = int(two_at + (seconds + 10) * CLOCK)
    cmd = [exe, "--timing", "386", "--engine", "interp", "--data", game, "--save", save,
           "--log", os.path.join(out, "run.log"), "--hold", "43",
           "--type", "SETUP.EXE+%d:n" % n_at, "--type", "SETUP.EXE+%d:2" % two_at,
           "--steps", str(steps), "--time-us", "700000000000000",
           "--record", os.path.join(out, "input.log"),
           "--shots-vga", os.path.join(shots, "shot"), "--shots-changed",
           "--frame-log", os.path.join(out, "frames.csv")] + (["--no-mouse"] if no_mouse else [])
    if opl:
        cmd += ["--opl-log", os.path.join(out, "opl.log")]
    r = subprocess.run(cmd, capture_output=True, text=True)
    open(os.path.join(out, "runner.txt"), "w").write(" ".join(cmd) + "\n" + r.stdout + r.stderr)
    if r.returncode:
        sys.exit("f117run failed (%d): see %s" % (r.returncode, os.path.join(out, "runner.txt")))
    json.dump(dict(data=data, exe=exe, seconds=seconds, no_mouse=no_mouse, command=cmd),
              open(os.path.join(out, "settings.json"), "w"), indent=1)


# ---- the two picture lists -------------------------------------------------------------------

def dac6(path):
    """A picture as 6-bit DAC values (and its digest), or None when it is not mode 13h."""
    im = Image.open(path)
    if im.size != (320, 200):
        return None, None
    a = np.asarray(im.convert("RGB"), dtype=np.uint8) >> 2
    return a, hashlib.sha1(a.tobytes()).hexdigest()


def box_pictures(trace):
    """86Box's picture changes (time s, frame, digest, path, instructions), SETUP's key time and the end.

    86Box posts no frame while the screen is off (ours shows the same periods as screen-off frames in its
    frame log), so a gap of more than one and a half frame periods between posted frames is a blank picture
    from the first missing frame on."""
    rows = [r for r in csv.reader(open(os.path.join(trace, "frames.csv")))]
    key_tsc = next(int(r[1]) for r in rows if int(r[0]) >= SETUP_FRAME)   # keys go in at the frame hook
    out, prev_hash, prev_tsc, ppm, digests = [], None, None, None, {}
    for r in rows:
        n, tsc, w, h, hsh, ins = int(r[0]), int(r[1]), r[3], r[4], r[5], int(r[6])
        graphics = (w, h) == ("640", "400")
        if graphics and hsh != prev_hash:
            ppm = os.path.join(trace, "f%06d.ppm" % n)       # the patch saves one at each change of hash
        gap = prev_tsc is not None and tsc - prev_tsc > 1.5 * FRAME
        blank_at = (prev_tsc + FRAME) / CLOCK if gap else None
        prev_hash, prev_tsc = hsh, tsc
        if n < SETUP_FRAME:
            continue
        if gap and out:
            out.append(dict(t=blank_at, frame=n, digest="blank", path=None, ins=ins))
        if not graphics:
            tok, path = "text", None
        else:
            if ppm not in digests:
                digests[ppm] = dac6(ppm)[1]
            tok, path = digests[ppm], ppm
        if (out and out[-1]["digest"] == tok) or (not out and tok == "text"):
            continue
        out.append(dict(t=tsc / CLOCK, frame=n, digest=tok, path=path, ins=ins))
    return out, key_tsc / CLOCK, int(rows[-1][1]) / CLOCK


def ours_pictures(run):
    """Our picture changes (time s, digest, path, steps), the 2 key's time and the end. Every VGA frame is in
    the frame log; a picture is written only when it changes, and a screen-off frame is a blank picture."""
    key = None
    for line in open(os.path.join(run, "input.log")):
        p = line.split()
        if p[0] == "K" and p[2] == "03":
            key = int(p[1]) / CLOCK
    out, cur, cur_path, end = [], None, None, 0.0
    for r in csv.DictReader(open(os.path.join(run, "frames.csv"))):
        t = int(r["frame_icount"]) / CLOCK
        end = t
        if r["written"] == "1":
            cur_path = os.path.join(run, "shots", "shot_%011d.ppm" % int(r["icount"]))
            cur = dac6(cur_path)[1] or "text"
        if key is None or int(r["icount"]) / CLOCK < key:
            continue
        if r.get("blank") == "1":
            tok, path = "blank", None
        else:
            tok, path = cur, (cur_path if cur != "text" else None)
        if (out and out[-1]["digest"] == tok) or (not out and tok in ("text", "blank")):
            continue
        out.append(dict(t=t, digest=tok, path=path, steps=int(r["steps"])))
    return out, key, end


# ---- the comparison ----------------------------------------------------------------------------

def mean_diff(p, q):
    a, _ = dac6(p)
    b, _ = dac6(q)
    return float(np.abs(a.astype(np.int16) - b.astype(np.int16)).mean())


def our_events(run):
    """What this machine's log says happened, with its clock: program starts and exits, overlays, file opens
    (the log's "@clock" lines). 86Box has no such view; the events place a step on our timeline."""
    out = []
    path = os.path.join(run, "run.log")
    if not os.path.exists(path):
        return out
    for line in open(path, errors="replace"):
        m = re.match(r"\[(exec|exit|overlay|file)\]\s+(.*?)\s+@(\d+)", line)
        if m:
            text = re.sub(r"\s+", " ", m.group(2))
            if m.group(1) == "exec":
                text = text.split(" psp=")[0]
            out.append((int(m.group(3)) / CLOCK, "%s %s" % (m.group(1), text)))
    return out


def frames(seconds):
    return seconds * CLOCK / FRAME


def compare(box, ours, box_key, ours_key, box_end, ours_end, events):
    sm = difflib.SequenceMatcher(None, [p["digest"] for p in box], [p["digest"] for p in ours], autojunk=False)
    match = {}
    for a, b, n in sm.get_matching_blocks():
        for k in range(n):
            if box[a + k]["digest"] != "text":
                match[a + k] = b + k
    if not match:
        sys.exit("no picture in common")
    # the origin: the first graphics picture both runs show (86Box's earlier ones are kept, as unmatched)
    first = min(match)
    b0, o0 = box[first]["t"], ours[match[first]]["t"]
    span = min(box_end - b0, ours_end - o0)        # the span both runs cover
    box = [p for p in box if p["t"] - b0 <= span]
    match = {i: j for i, j in match.items() if i < len(box) and ours[j]["t"] - o0 <= span}
    ours = [p for p in ours if p["t"] - o0 <= span]
    for i, p in enumerate(box):
        p["held"] = (box[i + 1]["t"] if i + 1 < len(box) else b0 + span) - p["t"]
    rows, prev_o = [], 0
    for i, p in enumerate(box):
        row = dict(i=i, frame=p["frame"], box_t=p["t"] - b0, held=p["held"], kind="text" if p["digest"] == "text" else "")
        if i in match:
            o = ours[match[i]]
            d = (p["t"] - b0) - (o["t"] - o0)
            row.update(kind="exact", ours_i=match[i], ours_t=o["t"] - o0, drift_ms=d * 1000, drift_frames=frames(d),
                       box_ins=p["ins"], ours_steps=o["steps"])
            prev_o = match[i]
        elif row["kind"] != "text" and p["path"]:
            # the nearest of our pictures by content between the matched neighbours, for the record
            nxt = next((match[j] for j in range(i + 1, len(box)) if j in match), len(ours))
            cand = [k for k in range(prev_o, min(nxt + 1, len(ours))) if ours[k]["path"]][:60]
            best = min(((mean_diff(p["path"], ours[k]["path"]), k) for k in cand), default=(None, None))
            row.update(kind="unmatched", nearest_diff=best[0], nearest_i=best[1])
        elif row["kind"] != "text":
            row.update(kind="unmatched")
        if p["digest"] == "blank":
            row["kind"] += " blank"
        rows.append(row)
    exact = [r for r in rows if r["kind"].startswith("exact")]
    # steps: consecutive matched pictures between which the drift moved by two frames or more. A one-frame
    # move is counted apart: animations on both machines alternate 2- and 3-frame holds, and the alternation's
    # phase can differ by a frame while the scene's drift stays put.
    steps, flips = [], 0
    for a, b in zip(exact, exact[1:]):
        dd = round(b["drift_frames"]) - round(a["drift_frames"])
        if abs(dd) == 1:
            flips += 1
        if abs(dd) >= 2:
            lo, hi = a["ours_t"] + o0, b["ours_t"] + o0
            steps.append(dict(from_i=a["i"], to_i=b["i"], from_frame=a["frame"], to_frame=b["frame"],
                              box_t=a["box_t"], box_s=b["box_t"] - a["box_t"], ours_s=b["ours_t"] - a["ours_t"],
                              added_ms=b["drift_ms"] - a["drift_ms"], added_frames=dd,
                              box_ins=b["box_ins"] - a["box_ins"], ours_steps=b["ours_steps"] - a["ours_steps"],
                              events=["%.3f %s" % (t - o0, e) for t, e in events if lo <= t < hi]))
    # scenes: from each picture held a second or more to the next
    starts = [r["i"] for r in rows if r["held"] >= HELD and r["kind"] != "text"]
    if not starts or starts[0] != 0:
        starts = [0] + starts
    scenes = []
    for s, i in enumerate(starts):
        j = starts[s + 1] if s + 1 < len(starts) else len(rows)
        part = rows[i:j]
        graphics = [r for r in part if r["kind"] != "text"]
        ex = [r["drift_ms"] for r in part if r["kind"].startswith("exact")]
        scenes.append(dict(scene=s + 1, box_t=rows[i]["box_t"], frame=rows[i]["frame"],
                           seconds=(rows[j]["box_t"] if j < len(rows) else span) - rows[i]["box_t"],
                           pictures=len(graphics), exact=len(ex),
                           drift_first_ms=ex[0] if ex else None, drift_median_ms=float(np.median(ex)) if ex else None,
                           drift_last_ms=ex[-1] if ex else None))
    prev = 0.0
    for sc in scenes:
        if sc["drift_median_ms"] is not None:
            sc["added_ms"] = sc["drift_median_ms"] - prev
            prev = sc["drift_median_ms"]
    graphics = [r for r in rows if r["kind"] != "text"]
    held = [r for r in graphics if frames(r["held"]) >= 1.5]
    on_frame = [r for r in exact if abs(r["drift_frames"]) < 0.5]
    first_off = next((r for r in exact if abs(r["drift_frames"]) >= 0.5), None)
    first_miss = next((r for r in held if not r["kind"].startswith("exact")), None)
    result = dict(
        span_s=span, box_pictures=len(graphics), ours_pictures=len([p for p in ours if p["path"]]), exact=len(exact),
        held=len(held), exact_held=sum(1 for r in held if r["kind"].startswith("exact")), on_frame=len(on_frame),
        unmatched_before_origin=first, origin=dict(box_tsc=int(round(b0 * CLOCK)), ours_clock=int(round(o0 * CLOCK))),
        blanks=dict(box=sum(1 for p in box if p["digest"] == "blank"), ours=sum(1 for p in ours if p["digest"] == "blank")),
        scene0=dict(box_s=b0 - box_key, ours_s=o0 - ours_key, diff_ms=((b0 - box_key) - (o0 - ours_key)) * 1000),
        drift_end_ms=exact[-1]["drift_ms"], drift_min_ms=min(r["drift_ms"] for r in exact),
        drift_max_ms=max(r["drift_ms"] for r in exact), one_frame_flips=flips,
        steps_added_ms=sum(s["added_ms"] for s in steps),
        first_off_frame=first_off, first_unmatched_held=first_miss, steps=steps, scenes=scenes)
    result["verdict"] = "PASS" if (len(exact) == len(graphics) and len(on_frame) == len(exact)) else "FAIL"
    return result, rows


def report(result, rows, out):
    r = result
    print("frames386  %s  %.1f s from the first common graphics picture: 86Box %d picture changes, ours %d; "
          "%d exact in order (%d of the %d held 2+ frames), %d of the exact on the same frame" % (
              r["verdict"], r["span_s"], r["box_pictures"], r["ours_pictures"], r["exact"], r["exact_held"], r["held"],
              r["on_frame"]))
    s0 = r["scene0"]
    print("scene 0 (SETUP's last key to the first common picture): 86Box %.3f s, ours %.3f s: 86Box later by %.1f ms%s" % (
        s0["box_s"], s0["ours_s"], s0["diff_ms"],
        "; 86Box showed %d picture(s) before it" % r["unmatched_before_origin"] if r["unmatched_before_origin"] else ""))
    print("blank (screen-off) periods: 86Box %d, ours %d" % (r["blanks"]["box"], r["blanks"]["ours"]))
    print("drift (86Box minus ours since that picture): end %+.1f ms (%+.1f frames), range %+.1f..%+.1f ms; "
          "%d steps of 2+ frames add %+.1f ms; %d one-frame flips" % (
              r["drift_end_ms"], frames(r["drift_end_ms"] / 1000), r["drift_min_ms"], r["drift_max_ms"], len(r["steps"]),
              r["steps_added_ms"], r["one_frame_flips"]))
    f = r["first_off_frame"]
    if f:
        print("first picture on another frame: 86Box change #%d (frame %d) at %.3f s, %+.1f ms (%+.2f frames)" % (
            f["i"], f["frame"], f["box_t"], f["drift_ms"], f["drift_frames"]))
    m = r["first_unmatched_held"]
    if m:
        print("first held picture with no exact copy: 86Box change #%d (frame %d) at %.3f s, nearest mean difference %s" % (
            m["i"], m["frame"], m["box_t"], "%.3f" % m["nearest_diff"] if m.get("nearest_diff") is not None else "- (%s)" % m["kind"]))
    print("\nsteps (consecutive exact pictures whose drift moved 2+ frames):")
    print("%8s %13s %9s %9s %10s %12s %11s  %s" % ("86Box s", "86Box frames", "86Box dt", "ours dt", "added ms",
                                                 "86Box instr", "ours steps", "our events in the interval"))
    for s in r["steps"]:
        print("%8.3f %6d-%-6d %9.3f %9.3f %+10.1f %12d %11d  %s" % (
            s["box_t"], s["from_frame"], s["to_frame"], s["box_s"], s["ours_s"], s["added_ms"], s["box_ins"], s["ours_steps"],
            "; ".join(s["events"]) or "-"))
    print("\n%-5s %8s %7s %7s %9s %6s %10s %10s %10s %9s" % ("scene", "86Box s", "frame", "length", "pictures", "exact",
                                                            "first ms", "median ms", "last ms", "added ms"))
    fmt = lambda v: "-" if v is None else "%+.1f" % v  # noqa: E731
    for sc in r["scenes"]:
        print("%-5d %8.2f %7d %7.2f %9d %6d %10s %10s %10s %9s" % (
            sc["scene"], sc["box_t"], sc["frame"], sc["seconds"], sc["pictures"], sc["exact"], fmt(sc["drift_first_ms"]),
            fmt(sc["drift_median_ms"]), fmt(sc["drift_last_ms"]), fmt(sc.get("added_ms"))))
    json.dump(result, open(os.path.join(out, "report.json"), "w"), indent=1)
    keys = ["i", "frame", "box_t", "held", "kind", "ours_i", "ours_t", "drift_ms", "drift_frames", "box_ins", "ours_steps",
            "nearest_diff", "nearest_i"]
    with open(os.path.join(out, "pairs.csv"), "w", newline="") as fh:
        w = csv.DictWriter(fh, keys)
        w.writeheader()
        for row in rows:
            w.writerow({k: ("%.6f" % v if isinstance(v, float) else v) for k, v in row.items() if k in keys})
    print("\nreport: %s, %s" % (os.path.join(out, "report.json"), os.path.join(out, "pairs.csv")))


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("out")
    ap.add_argument("--seconds", type=int, default=130, help="emulated seconds after SETUP's last key")
    ap.add_argument("--box", help="an earlier 86Box run (its OUT/box) instead of a new one")
    ap.add_argument("--ours", help="an earlier run of ours (its OUT/ours) instead of a new one")
    ap.add_argument("--profile", default=probe86.REFERENCE, help="copied per run, never changed")
    ap.add_argument("--data", default=r"D:\GOG\F-117A", help="the install, copied per run, never written")
    ap.add_argument("--exe", default=os.path.join(ROOT, "build", "f117run.exe"))
    ap.add_argument("--mouse-driver", help="86Box: put this CTMOUSE.EXE on the disk and load it (mouse_type msserial)")
    ap.add_argument("--only", choices=("box", "ours"), help="make that run only (two can go side by side), no comparison")
    ap.add_argument("--opl", action="store_true", help="also record ours/opl.log for reference sound checks")
    ap.add_argument("--ours-mouse", action="store_true",
                    help="ours keeps its INT 33h driver although 86Box's run had none (by default ours matches 86Box's run)")
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    box_dir = a.box or os.path.join(a.out, "box")
    ours_dir = a.ours or os.path.join(a.out, "ours")
    if not a.box and a.only != "ours":
        os.makedirs(box_dir, exist_ok=True)
        run_box(box_dir, a.profile, a.seconds, a.mouse_driver)
    if not a.ours and a.only != "box":
        os.makedirs(ours_dir, exist_ok=True)
        box_set = os.path.join(box_dir, "settings.json")
        box_mouse = os.path.exists(box_set) and json.load(open(box_set)).get("mouse_driver")
        # the VM loads no mouse driver unless asked (mouse_type none, probe86.bare_boot): INT 33h then reaches
        # the BIOS's dummy handler, which f117run --no-mouse answers as
        run_ours(ours_dir, a.data, os.path.abspath(a.exe), a.seconds,
                 no_mouse=not (box_mouse or a.ours_mouse), opl=a.opl)
    if a.only:
        return 0
    box, box_key, box_end = box_pictures(os.path.join(box_dir, "trace"))
    ours, ours_key, ours_end = ours_pictures(ours_dir)
    if not box or not ours:
        sys.exit("no graphics pictures after SETUP (86Box %d, ours %d)" % (len(box), len(ours)))
    result, rows = compare(box, ours, box_key, ours_key, box_end, ours_end, our_events(ours_dir))
    result.update(box=os.path.abspath(box_dir), ours=os.path.abspath(ours_dir))
    report(result, rows, a.out)
    return 0 if result["verdict"] == "PASS" else 1


if __name__ == "__main__":
    sys.exit(main())
