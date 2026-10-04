#!/usr/bin/env python3
"""Compare the hands-off intro's RGB frames with GOG DOSBox's ZMBV capture.

    py tools/video_compare.py --data DIR [--seconds 130]
    py tools/video_compare.py --reuse RUN_DIR

Captures stay outside the repository. Identical consecutive pictures are
collapsed, retaining their times and durations. Exact RGB hashes are aligned
in order; unmatched pictures are reported, never hidden by a tolerance.
Sampling differs: DOSBox captures VGA scanout, --shots takes instantaneous
VRAM snapshots. A mismatch is evidence to investigate, not automatically a
translation defect. This checks graphics (320x200), not text-mode setup.
"""
from __future__ import annotations

import argparse
from difflib import SequenceMatcher
import hashlib
import json
from pathlib import Path
import shutil
import statistics
import subprocess
import tempfile

from PIL import Image, ImageChops, ImageDraw

from dosbox_compare import IPS, ROOT, run_dosbox

WORK = Path.home() / "f117-recomp-local" / "video"


def append_frame(out, rgb, when, source, duration):
    digest = hashlib.sha256(rgb).hexdigest()
    if out and out[-1]["hash"] == digest:
        out[-1]["end"] = when + duration
        out[-1]["samples"] += 1
    else:
        out.append(dict(hash=digest, time=when, end=when + duration,
                        samples=1, source=str(source)))


def reference_frames(run):
    out, offset = [], 0.0
    for avi in sorted((run / "capture").glob("*.avi")):
        info = json.loads(subprocess.check_output([
            "ffprobe", "-v", "error", "-select_streams", "v:0",
            "-show_streams", "-of", "json", str(avi)]))["streams"][0]
        width, height = info["width"], info["height"]
        num, den = map(int, info["r_frame_rate"].split("/"))
        period = den / num
        # Mode 13h has both double flags set: DOSBox's raw capture keeps
        # it at 320x200. Never resize pixels or include text-mode captures.
        if (width, height) != (320, 200):
            print("  skipping non-mode-13h capture", avi.name, width, height)
            continue
        proc = subprocess.Popen([
            "ffmpeg", "-v", "error", "-i", str(avi), "-map", "0:v:0",
            "-f", "rawvideo",
            "-pix_fmt", "rgb24", "-"], stdout=subprocess.PIPE)
        frame = 0
        try:
            while True:
                rgb = proc.stdout.read(320 * 200 * 3)
                if not rgb:
                    break
                if len(rgb) != 320 * 200 * 3:
                    raise RuntimeError("truncated decoded frame")
                append_frame(out, rgb, offset + frame * period,
                             f"{avi.name}:{frame}", period)
                frame += 1
        finally:
            proc.stdout.close()
            rc = proc.wait()
        if rc:
            raise RuntimeError("ffmpeg decode failed")
        offset += frame * period
        print(f"  {avi.name}: {frame} frames, {1 / period:.6f} fps")
    if not out:
        raise RuntimeError("no graphics frames captured")
    return out


def our_frames(run, every):
    out = []
    for path in sorted((run / "shots").glob("*.ppm")):
        with Image.open(path) as im:
            if im.size != (320, 200):
                continue
            rgb = im.convert("RGB").tobytes()
        when = int(path.stem.rsplit("_", 1)[1]) / IPS
        append_frame(out, rgb, when, path, every / IPS)
    if not out:
        raise RuntimeError("no graphics shots produced")
    return out


def compare(ref, ours):
    blocks = SequenceMatcher(None, [f["hash"] for f in ref],
                             [f["hash"] for f in ours], autojunk=False).get_matching_blocks()
    matches = [(a + k, b + k) for a, b, n in blocks for k in range(n)]
    if not matches:
        raise RuntimeError("no exact RGB pictures in common")
    # A long initial black frame has an uncertain onset; use the median
    # offset of matching changes rather than anchoring on capture start.
    base = statistics.median(ref[r]["time"] - ours[o]["time"] for r, o in matches)
    lower, upper = ref[0]["time"] - base, ref[-1]["end"] - base
    matched_r, matched_o = {r for r, _ in matches}, {o for _, o in matches}
    missing_r = [i for i in range(len(ref)) if i not in matched_r]
    inside_o = {i for i, f in enumerate(ours) if f["end"] > lower and f["time"] < upper}
    missing_o = sorted(inside_o - matched_o)
    drift = [ref[r]["time"] - ours[o]["time"] - base for r, o in matches]
    result = dict(reference_pictures=len(ref), our_pictures=len(ours),
                  exact_matches=len(matches), span=ref[-1]["end"] - ref[0]["time"],
                  reference_unmatched=missing_r, our_unmatched=missing_o,
                  our_excluded=[sum(f["end"] <= lower for f in ours),
                                sum(f["time"] >= upper for f in ours)],
                  offset=base,
                  reference_multiframe_unmatched=[i for i in missing_r if ref[i]["samples"] > 1],
                  our_multiframe_unmatched=[i for i in missing_o if ours[i]["samples"] > 1],
                  drift_min=min(drift), drift_max=max(drift), drift_end=drift[-1])
    print(f"{len(matches)} exact RGB pictures in order over {result['span']:.3f} s")
    print(f"unmatched inside alignment: DOSBox {len(missing_r)}, here {len(missing_o)}")
    print("our pictures outside capture time range (before/after):", result["our_excluded"])
    print("unmatched lasting multiple samples: DOSBox %d, here %d" % (
        len(result["reference_multiframe_unmatched"]), len(result["our_multiframe_unmatched"])))
    print("timing drift (ms): min %+.3f, max %+.3f, end %+.3f" % (
        min(drift) * 1000, max(drift) * 1000, drift[-1] * 1000))
    for label, frames, missing in (("DOSBox", ref, missing_r), ("here", ours, missing_o)):
        for i in missing[:12]:
            f = frames[i]
            print(f"  {label} unmatched {i}: {f['time']:.3f}s, "
                  f"{f['end'] - f['time']:.3f}s, {f['source']}")
    return result


def diagnostic_images(run, ref, ours, result):
    """Show the longest differences first, paired by the estimated time.

    These pairs aid investigation; the nearest shot is not a parity verdict.
    Left: DOSBox. Middle: here. Right: absolute per-channel difference.
    """
    folder = run / "differences"
    folder.mkdir(exist_ok=True)
    candidates = []
    for side, frames, key in (("reference", ref, "reference_unmatched"),
                               ("ours", ours, "our_unmatched")):
        ids = sorted(result[key], key=lambda i: frames[i]["end"] - frames[i]["time"], reverse=True)
        candidates.extend((side, i) for i in ids[:6])
    for side, index in candidates:
        if side == "reference":
            ri = index
            oi = min(range(len(ours)), key=lambda i: abs(ours[i]["time"] - ref[ri]["time"] + result["offset"]))
        else:
            oi = index
            ri = min(range(len(ref)), key=lambda i: abs(ref[i]["time"] - ours[oi]["time"] - result["offset"]))
        avi, number = ref[ri]["source"].rsplit(":", 1)
        raw = subprocess.check_output([
            "ffmpeg", "-v", "error", "-i", str(run / "capture" / avi),
            "-vf", f"select=eq(n\\,{number})", "-frames:v", "1",
            "-f", "rawvideo", "-pix_fmt", "rgb24", "-"])
        left = Image.frombytes("RGB", (320, 200), raw)
        with Image.open(ours[oi]["source"]) as shot:
            middle = shot.convert("RGB")
        canvas = Image.new("RGB", (960, 230))
        canvas.paste(left, (0, 0))
        canvas.paste(middle, (320, 0))
        canvas.paste(ImageChops.difference(left, middle), (640, 0))
        ImageDraw.Draw(canvas).text((0, 205),
            f"DOSBox #{ri} {ref[ri]['time']:.3f}s | here #{oi} {ours[oi]['time']:.3f}s | RGB difference",
            fill="white")
        canvas.save(folder / f"{side}-{index:05d}.png")
    print("Diagnostic images:", folder)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--data", help="GOG install with DOSBOX and dosboxF117A.conf")
    ap.add_argument("--seconds", type=int, default=130)
    ap.add_argument("--reuse", type=Path, help="recompare an existing run")
    ap.add_argument("--diagnostics", action="store_true", help="write up to 12 paired difference PNGs")
    a = ap.parse_args()
    if a.seconds <= 0:
        ap.error("--seconds must be positive")
    every = IPS // 70
    if a.reuse:
        run = a.reuse.resolve()
        every = json.loads((run / "settings.json").read_text())["every"]
    else:
        if not a.data:
            ap.error("--data is required unless --reuse is given")
        WORK.mkdir(parents=True, exist_ok=True)
        run = Path(tempfile.mkdtemp(prefix="intro-", dir=WORK))
        print("Artifacts:", run, flush=True)
        (run / "settings.json").write_text(json.dumps(dict(seconds=a.seconds, every=every)))
        game = run / "game"
        game.mkdir()
        for path in Path(a.data).iterdir():
            if path.is_file() and not path.name.lower().startswith(
                    ("unins", "goggame", "gog", "launch", "support")):
                shutil.copy2(path, game)
        print(f"DOSBox: {a.seconds} s with video capture", flush=True)
        run_dosbox(a.data, str(game), a.seconds, capture="video", work=str(run))
        (run / "shots").mkdir()
        print("f117run: graphics shots at 70 Hz", flush=True)
        proc = subprocess.run([
            str(Path(ROOT) / "build" / "f117run.exe"), "--engine", "recomp",
            "--data", str(game), "--save", str(run / "save"),
            "--log", str(run / "run.log"), "--type", "SETUP.EXE+200000:N",
            "--type", "SETUP.EXE+2000000:2", "--steps", str((a.seconds + 15) * IPS),
            "--time-us", "700000000000000", "--shots", f"{every}:{run / 'shots' / 'shot'}"],
            capture_output=True, text=True)
        (run / "runner.txt").write_text(proc.stdout + proc.stderr)
        proc.check_returncode()
    ref, ours = reference_frames(run), our_frames(run, every)
    result = compare(ref, ours)
    (run / "comparison.json").write_text(json.dumps(dict(result=result, reference=ref, ours=ours), indent=2))
    if a.diagnostics:
        diagnostic_images(run, ref, ours, result)
    print("Report:", run / "comparison.json")
    return int(bool(result["reference_unmatched"] or result["our_unmatched"]))


if __name__ == "__main__":
    raise SystemExit(main())
