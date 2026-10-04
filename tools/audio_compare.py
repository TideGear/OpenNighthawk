#!/usr/bin/env python3
"""Measure rendered sound differences; requires ffmpeg and NumPy.

Envelope alignment estimates capture offset. It does not remove synthesizer,
gain or phase differences. Exact PCM equality is the only passing verdict.
"""
import argparse
import json
from pathlib import Path
import subprocess
import numpy as np


def decode(path):
    raw = subprocess.check_output(["ffmpeg", "-v", "error", "-i", str(path),
        "-map", "0:a:0", "-ac", "1", "-ar", "11025", "-f", "f32le", "-"])
    return np.frombuffer(raw, dtype="<f4").astype(np.float64)


def envelope(signal, block=110):
    return np.sqrt(np.mean(signal[:len(signal) // block * block].reshape(-1, block) ** 2, axis=1))


def align(left, right, limit=1000):
    """Lag is the number of right samples skipped relative to left."""
    left = left - left.mean(); right = right - right.mean()
    n = 1 << (len(left) + len(right) - 1).bit_length()
    conv = np.fft.irfft(np.fft.rfft(right, n) * np.fft.rfft(left[::-1], n), n)
    lags = np.arange(max(-len(left) + 1, -limit), min(len(right), limit + 1))
    return int(lags[np.argmax(conv[lags + len(left) - 1])])


def compare(left, right, rate=11025):
    if len(left) < rate or len(right) < rate:
        raise ValueError("at least one second of sound required")
    if not np.any(left) or not np.any(right):
        raise ValueError("silent capture cannot establish sound parity")
    lag = align(envelope(left), envelope(right)) * 110
    lstart, rstart = max(0, -lag), max(0, lag)
    length = min(len(left) - lstart, len(right) - rstart)
    a, b = left[lstart:lstart + length], right[rstart:rstart + length]
    ea, eb = envelope(a), envelope(b)
    spectral = []
    window = np.hanning(rate)
    for at in range(0, length - rate + 1, rate):
        x, y = a[at:at + rate], b[at:at + rate]
        if np.mean(x*x) < 1e-9 or np.mean(y*y) < 1e-9:
            continue
        sx, sy = np.abs(np.fft.rfft(x * window)), np.abs(np.fft.rfft(y * window))
        spectral.append(float(np.dot(sx, sy) / (np.linalg.norm(sx) * np.linalg.norm(sy))))
    return {"exact_pcm_equal": bool(np.array_equal(a, b)), "aligned_seconds": length / rate,
        "estimated_ours_offset_seconds": lag / rate,
        "envelope_correlation": float(np.corrcoef(ea, eb)[0, 1]),
        "waveform_correlation": float(np.corrcoef(a, b)[0, 1]),
        "reference_rms": float(np.sqrt(np.mean(a*a))), "ours_rms": float(np.sqrt(np.mean(b*b))),
        "spectral_cosine_median": float(np.median(spectral)) if spectral else None,
        "limitations": "mono/resampled diagnostic; approximate envelope alignment; no listening verdict"}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reference", required=True)
    parser.add_argument("--ours", required=True)
    parser.add_argument("--out", required=True)
    args = parser.parse_args()
    result = compare(decode(args.reference), decode(args.ours))
    Path(args.out).write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(result, indent=2))
    return int(not result["exact_pcm_equal"])


if __name__ == "__main__":
    raise SystemExit(main())
