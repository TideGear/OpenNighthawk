#!/usr/bin/env python3
"""Render a --sound-sweep (f117run) into one short audio file per sound id (docs/sound-board.md).

    sound_board_render.py --kind adlib|roland|speaker --sweep DIR --prefix a_ --out OUT
                          [--audio-render PATH] [--roms DIR] [--ips 9000000] [--speaker-model realsound|pwm]

DIR holds PREFIXsweep.tsv and PREFIX<id>.opl/.midi/.spk from the sweep. Each id's logs are rebased so the
request is at clock 0, rendered with tools/audio_render (the application's own audio path; Munt with the MT-32
ROMs in --roms for the Roland driver), then trimmed of leading and trailing silence, mixed to mono and encoded
(MP3 with ffmpeg when present, else WAV). OUT/index.json lists id, file, seconds and the sweep's write counts.
"""
import argparse
import json
import os
import shutil
import struct
import subprocess
import sys
import wave

SILENCE = 150            # |sample| at or below this counts as silence (16-bit)
PAD = 0.05               # seconds of silence kept either side


def rebase(src, dst, start, speaker=False):
    n = 0
    with open(src) as f, open(dst, "w") as o:
        for line in f:
            p = line.split()
            if not p:
                continue
            p[0] = str(max(0, int(p[0]) - start))
            if speaker and len(p) >= 5:
                p[4] = str(max(0, int(p[4]) - start))
            o.write(" ".join(p) + "\n")
            n += 1
    return n


def trim_mono(src_wav, dst_wav):
    w = wave.open(src_wav)
    ch, rate, n = w.getnchannels(), w.getframerate(), w.getnframes()
    d = struct.unpack("<%dh" % (n * ch), w.readframes(n))
    w.close()
    mono = [sum(d[i:i + ch]) // ch for i in range(0, len(d), ch)] if ch > 1 else list(d)
    loud = [i for i, x in enumerate(mono) if abs(x) > SILENCE]
    if not loud:
        return 0.0
    a = max(0, loud[0] - int(PAD * rate))
    b = min(len(mono), loud[-1] + int(PAD * rate))
    out = wave.open(dst_wav, "wb")
    out.setnchannels(1)
    out.setsampwidth(2)
    out.setframerate(rate)
    out.writeframes(struct.pack("<%dh" % (b - a), *mono[a:b]))
    out.close()
    return (b - a) / rate


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--kind", required=True, choices=["adlib", "roland", "speaker"])
    ap.add_argument("--sweep", required=True)
    ap.add_argument("--prefix", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--audio-render", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "build", "audio_render.exe"))
    ap.add_argument("--roms", default=os.path.expanduser("~/f117-recomp-local/roms"))
    ap.add_argument("--ips", type=int, default=9000000)
    ap.add_argument("--speaker-model", default="realsound")
    ap.add_argument("--only", help="comma list of ids")
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    work = os.path.join(a.out, "_work")
    os.makedirs(work, exist_ok=True)
    ffmpeg = shutil.which("ffmpeg")
    rows = [l.rstrip("\n").split("\t") for l in open(os.path.join(a.sweep, a.prefix + "sweep.tsv"))][1:]
    only = set(a.only.split(",")) if a.only else None
    index = []
    for id_, start, end, opl, midi, spk in rows:
        if only and id_ not in only:
            continue
        start, end = int(start), int(end)
        span = end - start
        base = os.path.join(a.sweep, a.prefix + id_)
        raw = os.path.join(work, "%s.wav" % id_)
        cmd = [a.audio_render]
        have = 0
        spk_re = os.path.join(work, id_ + ".spk")
        if int(spk):
            have += rebase(base + ".spk", spk_re, start, speaker=True)
        if a.kind == "roland":
            mid = os.path.join(work, id_ + ".midi")
            if not rebase(base + ".midi", mid, start) and not int(spk):
                continue
            cmd += [mid, raw, str(a.ips), str(span), "--mt32",
                    os.path.join(a.roms, "mt32_ctrl_1_07.rom"), os.path.join(a.roms, "mt32_pcm.rom"), "--seed", "1"]
        else:
            opl_re = os.path.join(work, id_ + ".opl")
            n = rebase(base + ".opl", opl_re, start)
            if not n and not int(spk):
                continue
            cmd += [opl_re, raw, str(a.ips), str(span), "dbopl"]
        if int(spk):
            cmd += ["--speaker-log", spk_re, "--speaker-model", a.speaker_model]
        r = subprocess.run(cmd, capture_output=True, text=True)
        if r.returncode or not os.path.exists(raw):
            print("id %s: audio_render failed: %s" % (id_, (r.stderr or r.stdout).strip()[:200]))
            continue
        trimmed = os.path.join(a.out, "%s-%s.wav" % (a.kind, id_.zfill(2)))
        secs = trim_mono(raw, trimmed)
        if not secs:
            print("id %s: silent" % id_)
            os.remove(trimmed) if os.path.exists(trimmed) else None
            continue
        name = os.path.basename(trimmed)
        if ffmpeg:
            mp3 = trimmed[:-4] + ".mp3"
            if subprocess.run([ffmpeg, "-y", "-loglevel", "error", "-i", trimmed, "-b:a", "64k", mp3]).returncode == 0:
                os.remove(trimmed)
                name = os.path.basename(mp3)
        index.append({"id": int(id_), "kind": a.kind, "file": name, "seconds": round(secs, 2),
                      "opl": int(opl), "midi": int(midi), "speaker": int(spk),
                      "capped": span and secs >= (span / a.ips) - 0.1})
        print("id %s: %.2f s" % (id_, secs))
    with open(os.path.join(a.out, "index.json"), "w") as f:
        json.dump(index, f, indent=1)
    shutil.rmtree(work, ignore_errors=True)
    print("%d sounds rendered" % len(index))


if __name__ == "__main__":
    sys.exit(main())
