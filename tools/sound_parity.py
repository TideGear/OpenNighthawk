#!/usr/bin/env python3
"""sound_parity.py - this machine's intro music held to a reference capture's sound.

    py tools/sound_parity.py --avi CAPTURE.avi --opl-log RUN/opl.log --reference gog|dosbox-x [--ips 9000000] [--seconds 145]

Renders the AdLib log from a video_compare run (f117run --opl-log) with the
application's own audio path (build/audio_render), then compares it with the
audio track of the reference's capture (GOG DOSBox, DOSBox-X): envelope and
spectral similarity and the RMS ratio, after aligning for the capture's start.
Exact PCM is not expected (different OPL cores and mixers; the DOSBoxes use
DBOPL, as does this machine, so the music matches closely). The verdict is
against the figures at the top of the file. Exit 0 when within them.
"""
import argparse
import json
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import audio_compare  # noqa: E402

ROOT = HERE.parent
# Measured 6 Oct 2026 (intro, ~125-130 s). GOG's capture: envelope 0.963, spectral 0.992, level
# 1.005. DOSBox-X: envelope 0.89-0.90, spectral 0.947-0.949, and its mixer runs the music at 0.74
# of the level this machine (and GOG) produce, so its level is reported, not judged (rms_ratio None).
LIMITS = {
    "gog": dict(min_envelope=0.95, min_spectral=0.98, rms_ratio=(0.95, 1.05), max_offset_s=3.0),
    "dosbox-x": dict(min_envelope=0.85, min_spectral=0.93, rms_ratio=None, max_offset_s=3.0),
}


def render(opl_log, ips, end_clock, wav):
    subprocess.run([str(ROOT / "build" / "audio_render.exe"), str(opl_log), str(wav), str(ips), str(end_clock)],
                   check=True, stdout=subprocess.DEVNULL)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--reference", required=True, choices=sorted(LIMITS))
    ap.add_argument("--avi", required=True, type=Path)
    ap.add_argument("--opl-log", required=True, type=Path)
    ap.add_argument("--ips", type=int, default=9000000)
    ap.add_argument("--seconds", type=int, default=145, help="how much of the run to render")
    ap.add_argument("--json", type=Path)
    a = ap.parse_args()
    with tempfile.TemporaryDirectory() as t:
        wav = Path(t) / "ours.wav"
        render(a.opl_log, a.ips, a.seconds * a.ips, wav)
        ref = audio_compare.decode(a.avi)
        ours = audio_compare.decode(wav)
    r = audio_compare.compare(ref, ours)
    ratio = r["ours_rms"] / r["reference_rms"]
    lim = LIMITS[a.reference]
    level = "" if lim["rms_ratio"] is None else " (%.2f-%.2f)" % lim["rms_ratio"]
    ok = (r["envelope_correlation"] >= lim["min_envelope"] and (r["spectral_cosine_median"] or 0) >= lim["min_spectral"]
          and (lim["rms_ratio"] is None or lim["rms_ratio"][0] <= ratio <= lim["rms_ratio"][1])
          and abs(r["estimated_ours_offset_seconds"]) <= lim["max_offset_s"])
    print("sound %-8s %s  envelope %.4f (>= %.2f), spectral %.4f (>= %.2f), level %.4f%s, offset %+.2f s (|.| <= %.1f), %.0f s compared" % (
        a.reference, "PASS" if ok else "FAIL", r["envelope_correlation"], lim["min_envelope"], r["spectral_cosine_median"] or 0,
        lim["min_spectral"], ratio, level or " (informational)", r["estimated_ours_offset_seconds"], lim["max_offset_s"],
        r["aligned_seconds"]))
    if a.json:
        a.json.write_text(json.dumps(r, indent=1))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
