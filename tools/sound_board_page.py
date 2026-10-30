#!/usr/bin/env python3
"""Build the sound board page (docs/sound-board.md) as one self-contained HTML file.

    sound_board_page.py --board DIR --mac DIR --amiga DIR --speech DIR --out FILE.html

DIR for --board holds adlib/, roland/ and speaker/ (each the output of sound_board_render.py, with its
index.json); --mac and --amiga hold the decoded WAVs; --speech holds the nine cut words. Every clip is
encoded to mono MP3 with ffmpeg and embedded as base64, so the page needs no other file. The audio never goes
in the repository: only this script and tools/sound_board_template.html do.
"""
import argparse
import base64
import glob
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import wave

HERE = os.path.dirname(os.path.abspath(__file__))

MAC_NAMES = {
    304: "Large explosion", 305: "Small explosion", 306: "Cannon", 307: "Blue warning", 308: "Yellow warning",
    309: "Red warning", 310: "Tire screech", 311: "Engine spool up", 312: "Engine spool down", 313: "Missile",
    314: "Stall horn", 315: "Drop chaff", 316: "Drop bomb", 317: "Catapult", 318: "Camera", 319: "Landing gear",
    320: "Eject",
}
MAC_OTHER = {
    200: "Title music", 321: "Death music", 322: "Good mission", 323: "Bay door", 324: "Hangar door",
    325: "Impact", 500: "Jet sound",
}
AMIGA_NAMES = {
    "f117_0_beep1": ("Front-end beep 1", "front end"), "f117_1_beep2": ("Front-end beep 2", "front end"),
    "play2_0_explosion": ("Explosion", "intro player"), "play2_1_firemissile": ("Missile fire", "intro player"),
    "play2_2_introbeep": ("Intro beep", "intro player"),
}
# What each id is, from the calls in the original's programs (a census of every sound call in START, END, PLAYER,
# MPS_LOGO and the flight gate; docs/sound-board.md). "Mac" says which Mac resource the same event plays.
MEANING = {
    2: "palette fade (front-end screens)", 3: "intro music (PLAYER.EXE)",
    26: "button press / wait for a click", 27: "options screens: the highlight moves to a new item",
    28: "map marks drawn one by one (briefing map; debrief route)",
    29: "weapons page: a bay is chosen (Mac 323, bay door)", 30: "weapons page: a weapon is loaded into the bay (Mac 325, impact)",
    31: "launch animation (Mac 324, hangar door)", 33: "pilot options: a typed character",
    34: "weapons page: choice refused (bay already holds it)", 35: "cancel / dismiss",
    36: "awards screen music", 37: "debrief picture music", 38: "report picture music", 39: "pilot-landed screen music",
    21: "no caller found", 22: "no caller found", 32: "no caller found",
    40: "driver command (release the sound buffer when a flight ends), not a sound",
    23: "engine / jet", 24: "engine state, asked for every frame", 25: "engine state, asked for every frame",
}
GROUPS = [
    ("Music", {3, 36, 37, 38, 39}),
    ("Weapons, warnings and flight effects (Mac ids 304 to 320)", set(range(4, 21))),
    ("Engine", {23, 24, 25}),
    ("Menus, briefing, weapons page and launch", {26, 27, 28, 29, 30, 31, 33, 34, 35}),
    ("Speech phrases", {41, 42, 43, 44, 45}),
]
# the owner's rule: music from the Mac if there is a recording, else Roland, else AdLib; effects AdLib; speech the
# deeper voice, which the speaker and Roland drivers share (both play the speech through the PC speaker)
MAC_MUSIC = {36: 322, 37: 321, 38: 200}      # the owner matched these by ear (marks on the Mac rows)
SPEECH = ["primary", "target", "hit", "good", "nice", "landing", "cleared", "for take off", "to land"]


def encode(path, tmp):
    out = os.path.join(tmp, "c.mp3")
    r = subprocess.run(["ffmpeg", "-y", "-loglevel", "error", "-i", path, "-ac", "1", "-ar", "22050", "-b:a", "48k", out])
    if r.returncode:
        raise SystemExit("ffmpeg failed on " + path)
    with open(out, "rb") as f:
        return base64.b64encode(f.read()).decode()


def seconds(path):
    w = wave.open(path)
    return w.getnframes() / w.getframerate()


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--board", required=True)
    ap.add_argument("--mac", required=True)
    ap.add_argument("--amiga", help="Amiga samples (no longer shown by default; the owner judged them not worth using)")
    ap.add_argument("--speech", required=True)
    ap.add_argument("--board-long", help="a second board folder whose clips replace the same ids' (longer renders)")
    ap.add_argument("--board-held", help="a board folder of renders that ask for the sound again every step (sustained versions)")
    ap.add_argument("--out", required=True)
    a = ap.parse_args()
    if not shutil.which("ffmpeg"):
        raise SystemExit("ffmpeg is needed to encode the clips")
    tmp = tempfile.mkdtemp()
    clips, dos, speech, other = {}, [], [], []

    # DOS drivers: one clip per (driver, id)
    drivers = {}
    where = {}                      # (kind, id) -> the folder its file is in
    for kind in ("adlib", "roland", "speaker"):
        drivers[kind] = {}
        # a longer render of the same id (--board-long) replaces the standard one
        for root in (a.board, a.board_long):
            idx = os.path.join(root, kind, "index.json") if root else None
            if idx and os.path.exists(idx):
                for e in json.load(open(idx)):
                    drivers[kind][e["id"]] = e
                    where[(kind, e["id"])] = root
    held = {}
    for kind in ("adlib", "roland", "speaker"):
        idx = os.path.join(a.board_held, kind, "index.json") if a.board_held else None
        held[kind] = {e["id"]: e for e in json.load(open(idx))} if idx and os.path.exists(idx) else {}
    mac_by_id = {}
    for f in glob.glob(os.path.join(a.mac, "*.wav")):
        m = re.match(r"(\d+)_", os.path.basename(f))
        if m:
            mac_by_id[int(m.group(1))] = f
    silent = []
    for i in range(46):
        row = {"key": "dos-%d" % i, "title": "Sound %d" % i, "sub": "", "clips": {}}
        for kind, table in drivers.items():
            e = table.get(i)
            if not e:
                continue
            mp3 = os.path.join(where[(kind, i)], kind, e["file"])
            key = "%s-%d" % (kind, i)
            if mp3.endswith(".mp3"):
                clips[key] = {"b": base64.b64encode(open(mp3, "rb").read()).decode(), "s": e["seconds"], "c": bool(e.get("capped"))}
            else:
                clips[key] = {"b": encode(mp3, tmp), "s": e["seconds"], "c": bool(e.get("capped"))}
            row["clips"][kind] = key
            # the same sound asked for again every step, as the game does for a held sound: offered only when it
            # lasts meaningfully longer than the single request
            h = held.get(kind, {}).get(i)
            if h and h["seconds"] >= e["seconds"] + 0.4:
                hpath = os.path.join(a.board_held, kind, h["file"])
                hkey = "%sH-%d" % (kind, i)
                clips[hkey] = {"b": base64.b64encode(open(hpath, "rb").read()).decode(), "s": h["seconds"], "c": bool(h.get("capped"))}
                row["clips"][kind + "H"] = hkey
        if 300 + i in MAC_NAMES and (300 + i) in mac_by_id:
            key = "mac-%d" % (300 + i)
            clips[key] = {"b": encode(mac_by_id[300 + i], tmp), "s": round(seconds(mac_by_id[300 + i]), 2), "c": False}
            row["clips"]["mac"] = key
            row["sub"] = "Mac resource %d: %s" % (300 + i, MAC_NAMES[300 + i])
        if i in MAC_MUSIC and MAC_MUSIC[i] in mac_by_id:
            mk = "mac-%d" % MAC_MUSIC[i]
            if mk not in clips:
                clips[mk] = {"b": encode(mac_by_id[MAC_MUSIC[i]], tmp), "s": round(seconds(mac_by_id[MAC_MUSIC[i]]), 2), "c": False}
            row["clips"]["mac"] = mk
            row["sub"] = "Mac resource %d is the same music" % MAC_MUSIC[i]
        if not any(k in row["clips"] for k in ("adlib", "roland", "speaker")):
            silent.append(i)
            continue
        if i in MEANING:
            row["sub"] = (row["sub"] + "; " if row["sub"] else "") + MEANING[i]
        row["group"] = next((g for g, ids in GROUPS if i in ids), "Not identified yet")
        # the default source, in order of preference (the viewer can override per sound in the game)
        if i in {3, 36, 37, 38, 39}:
            prefer = ("mac", "roland", "adlib")             # music
        elif i in {41, 42, 43, 44, 45}:
            prefer = ("mac", "speaker")                      # speech: the Mac words stand in for a phrase
            if "mac" not in row["clips"]:
                row["clips"].pop("mac", None)
        else:
            prefer = ("mac", "adlib", "roland", "speaker")   # sounds (AdLib has nothing for a few ids)
        row["def"] = "mac" if (prefer[0] == "mac" and (("mac" in row["clips"]) or i in {41, 42, 43, 44, 45})) else             next((k for k in prefer[1:] if k in row["clips"]), "")
        dos.append(row)

    order = [g for g, _ in GROUPS] + ["Not identified yet"]
    dos.sort(key=lambda r: (order.index(r["group"]), int(r["key"].split("-")[1])))

    # speech: the DOS file's nine words, and the Mac's nine recordings
    for n, word in enumerate(SPEECH):
        row = {"key": "speech-%d" % (n + 1), "title": word.capitalize(), "sub": "word %d of 9" % (n + 1), "clips": {}}
        f = glob.glob(os.path.join(a.speech, "%d-*.wav" % (n + 1)))
        if f:
            clips["dos-speech-%d" % (n + 1)] = {"b": encode(f[0], tmp), "s": round(seconds(f[0]), 2), "c": False}
            row["clips"]["dos"] = "dos-speech-%d" % (n + 1)
        if 1000 + n in mac_by_id:
            clips["mac-%d" % (1000 + n)] = {"b": encode(mac_by_id[1000 + n], tmp), "s": round(seconds(mac_by_id[1000 + n]), 2), "c": False}
            row["clips"]["mac"] = "mac-%d" % (1000 + n)
        row["def"] = "mac" if "mac" in row["clips"] else "speaker"
        speech.append(row)

    # Mac sounds with no DOS id, and the Amiga samples
    for rid, name in MAC_OTHER.items():
        if rid in mac_by_id:
            clips["mac-%d" % rid] = {"b": encode(mac_by_id[rid], tmp), "s": round(seconds(mac_by_id[rid]), 2), "c": False}
            other.append({"key": "mac-%d" % rid, "def": "mac", "title": name, "sub": "Mac resource %d" % rid, "clips": {"mac": "mac-%d" % rid}})
    for f in (sorted(glob.glob(os.path.join(a.amiga, "*.wav"))) if a.amiga else []):
        stem = os.path.splitext(os.path.basename(f))[0]
        if stem.startswith("chipcand_"):
            name, where = ("Candidate " + stem[-2:].lstrip("0"), "raw 8-bit data from the flight program's chip-memory block, played at 8363 Hz: unverified, may not be a sound")
        else:
            name, where = AMIGA_NAMES.get(stem, (stem, "Amiga"))
        clips["amiga-" + stem] = {"b": encode(f, tmp), "s": round(seconds(f), 2), "c": False}
        other.append({"key": "amiga-" + stem, "title": name, "sub": "Amiga, " + where, "clips": {"amiga": "amiga-" + stem}})

    # what still needs the owner: each Mac recording beside the DOS sound its code matches, an id with no known
    # caller, and the one design choice. Everything else is handled and sits in a collapsed section.
    decide = []
    MATCHES = [(31, 323, "Strong match: the 8 key (bay doors) in flight plays DOS 31, and the Mac plays 323 at the same point, "
                         "both refused while a weapon is on its way. DOS also plays 31 for the launch animation."),
               (29, 325, "Weaker: on the weapons page DOS plays 29 when a bay is chosen. The Mac plays 323 there "
                         "(the same bay-door sound again) and 325 (impact) when a weapon is picked."),
               (30, 325, "Weaker: DOS plays 30 when a weapon is placed in the bay; the Mac plays 325 (impact) for the pick.")]
    for did, mid, why in MATCHES:
        row = next((r for r in dos if r["key"] == "dos-%d" % did), None)
        if row and ("mac-%d" % mid) in clips:
            dos.remove(row)
            row["clips"]["mac"] = "mac-%d" % mid
            row["sub"] = "Does Mac %d fit? " % mid + why
            decide.append(row)
    # the Mac's hangar door (324), played at the start of the launch animation, beside DOS 31's drivers
    d31 = next((r for r in decide if r["key"] == "dos-31"), None)
    if d31 and "mac-324" in clips:
        decide.insert(decide.index(d31) + 1, {"key": "mac-324", "title": "Mac 324, hangar door", "def": "mac",
            "sub": "Does it fit the launch animation? The Mac plays 324 as that animation starts; DOS plays 31 there (same sound as the bay doors).",
            "clips": dict(d31["clips"], mac="mac-324")})
    other[:] = [r for r in other if r["key"] not in ("mac-323", "mac-324", "mac-325")]
    row = next((r for r in dos if r["key"] == "dos-32"), None)
    if row:
        dos.remove(row)
        row["sub"] = "No caller found anywhere in the game. Does it sound like anything you know?"
        decide.append(row)
    decide.append({"key": "decide-split", "title": "Hybrid sound: first version", "clips": {}, "def": "",
                   "sub": "Ship Mac-first (the loaded DOS driver plays what has no recording), or build the Roland-for-music, AdLib-for-sounds split first? Type your choice in Note."})

    data = {"decide": decide, "clips": clips, "dos": dos, "speech": speech, "other": other, "silent": silent,
            "note": "The Mac effects line up with DOS ids 4 to 20 by the Mac port's id offset of 300 (docs/sound-board.md). "
                    "Roland clips use the MT-32 1.07 control ROM; speech under the Roland and speaker drivers plays through the PC speaker."}
    template = open(os.path.join(HERE, "sound_board_template.html"), encoding="utf-8").read()
    html = template.replace("/*__DATA__*/", json.dumps(data, separators=(",", ":")))
    with open(a.out, "w", encoding="utf-8") as f:
        f.write(html)
    shutil.rmtree(tmp, ignore_errors=True)
    print("%d clips, %d DOS rows, %d speech, %d other; %.1f MB" % (len(clips), len(dos), len(speech), len(other), len(html) / 1e6))


if __name__ == "__main__":
    sys.exit(main())
