#!/usr/bin/env python3
"""Compare a captured 86Box speaker call with this machine's rendered call.

Requires NumPy and ffmpeg. See build_86box.md for the silent capture hook.
Port data selects the same call in each recording; waveform metrics are
diagnostics, not a claim of PCM equality or a listening verdict.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import wave

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import audio_compare  # noqa: E402
import speaker_parity  # noqa: E402

RATE = 44100
PAD = 0.25


def longest_call(events):
    """Longest uninterrupted run of count writes, stripping leading 128 silence."""
    runs, current = [], []
    for time, event in events:
        if event[0] != "N":
            continue
        if current and time - current[-1][0] > 1:
            runs.append(current)
            current = []
        current.append((time, event[1]))
    if current:
        runs.append(current)
    if not runs:
        raise ValueError("no speaker count writes")
    call = max(runs, key=len)
    start = next((i for i, (_, value) in enumerate(call) if value != 128), len(call))
    call = call[start:]
    if len(call) < 2:
        raise ValueError("no non-silent call")
    return call


def clip(path, call, raw=False):
    start = call[0][0] / 1000 - PAD
    duration = (call[-1][0] - call[0][0]) / 1000 + 2 * PAD
    if start < 0:
        raise ValueError("call has less than 250 ms of pre-roll")
    command = ["ffmpeg", "-v", "error"]
    if raw:
        command += ["-f", "s32le", "-ar", "48000", "-ac", "2"]
    command += ["-ss", str(start), "-i", str(path), "-t", str(duration),
                "-ac", "1", "-ar", str(RATE), "-f", "f32le", "-"]
    samples = np.frombuffer(subprocess.check_output(command), dtype="<f4").astype(float)
    # 86Box's int32 mixer containers hold signed-16-bit amplitude units.
    # ffmpeg normalizes s32le by 2**31, so undo that extra 16-bit scaling.
    if raw:
        samples *= 65536
    if len(samples) < int((duration - 0.01) * RATE):
        raise ValueError(f"{path}: capture ends before the call's post-roll")
    if not np.any(samples):
        raise ValueError(f"{path}: silent audio")
    return samples


def bandpass(samples, low, high):
    frequencies = np.fft.rfftfreq(len(samples), 1 / RATE)
    spectrum = np.fft.rfft(samples)
    spectrum[(frequencies < low) | (frequencies > high)] = 0
    return np.fft.irfft(spectrum, n=len(samples))


def metrics(reference, ours, duration, low, high):
    a, b = bandpass(reference, low, high), bandpass(ours, low, high)
    candidates = []
    # Opposite speaker polarity is reported, not hidden by envelope alignment.
    for polarity in (1, -1):
        lag = audio_compare.align(a, polarity * b, RATE // 50)
        left, right = a[max(0, -lag):], b[max(0, lag):]
        count = min(len(left), len(right))
        # Exclude only the first/last 20 ms of the call's switching transient.
        first = int((PAD + 0.02) * RATE)
        last = min(count, int((PAD + duration - 0.02) * RATE))
        x, y = left[first:last], right[first:last]
        if len(x) < RATE // 10 or not np.any(x) or not np.any(y):
            raise ValueError("too little active speech for comparison")
        correlation = float(np.corrcoef(x, y)[0, 1])
        envelope = float(np.corrcoef(audio_compare.envelope(x), audio_compare.envelope(y))[0, 1])
        candidates.append(dict(band_hz=[low, high], lag_ms=lag * 1000 / RATE,
                               waveform_correlation=correlation,
                               envelope_correlation=envelope,
                               ours_over_reference_rms=float(np.sqrt(np.mean(y * y) / np.mean(x * x)))))
    return max(candidates, key=lambda entry: abs(entry["waveform_correlation"]))


def digest(path):
    sha = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            sha.update(block)
    return dict(path=str(path), bytes=path.stat().st_size, sha256=sha.hexdigest())


def write_wave(path, samples):
    with wave.open(str(path), "wb") as output:
        output.setnchannels(1)
        output.setsampwidth(2)
        output.setframerate(RATE)
        output.writeframes((np.clip(samples, -1, 1) * 32767).astype("<i2").tobytes())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("reference-pcm", "reference-ports", "ours-wav", "ours-ports", "out"):
        parser.add_argument("--" + name, required=True, type=Path)
    parser.add_argument("--ips", type=int, default=9_000_000)
    parser.add_argument("--capture-off", type=Path)
    parser.add_argument("--capture-on", type=Path)
    args = parser.parse_args()
    if bool(args.capture_off) != bool(args.capture_on):
        parser.error("capture-off and capture-on must be supplied together")
    speaker_parity.IPS = args.ips
    reference_call = longest_call(speaker_parity.dbx_events(args.reference_ports))
    ours_call = longest_call(speaker_parity.ours_events(args.ours_ports))
    if [value for _, value in reference_call] != [value for _, value in ours_call]:
        raise ValueError("speech count streams differ; these recordings do not establish waveform parity")
    reference = clip(args.reference_pcm, reference_call, raw=True)
    ours = clip(args.ours_wav, ours_call)
    duration = (reference_call[-1][0] - reference_call[0][0]) / 1000
    result = dict(counts_identical=True, counts=len(reference_call),
                  reference_start_ms=reference_call[0][0], ours_start_ms=ours_call[0][0],
                  reference_duration_ms=duration * 1000,
                  ours_duration_ms=ours_call[-1][0] - ours_call[0][0],
                  metrics=[metrics(reference, ours, duration, 300, 3400),
                           metrics(reference, ours, duration, 100, 8000)],
                  inputs=[digest(path) for path in (args.reference_pcm, args.reference_ports,
                                                   args.ours_wav, args.ours_ports)],
                  limitations="Band-limited mono diagnostic; polarity and level are reported. No PCM-equality or listening verdict.")
    if args.capture_off:
        checks = []
        for name in ("trace/frames.csv", "opl86.log", "speaker.log"):
            off, on = args.capture_off / name, args.capture_on / name
            a, b = digest(off), digest(on)
            if a["sha256"] != b["sha256"]:
                raise ValueError(f"capture changed {name}")
            checks.append(dict(name=name, identical=True, sha256=a["sha256"]))
        result["capture_invariance"] = checks
    args.out.mkdir(parents=True, exist_ok=True)
    write_wave(args.out / "reference-speech.wav", reference)
    write_wave(args.out / "ours-speech.wav", ours)
    (args.out / "report.json").write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(result, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
