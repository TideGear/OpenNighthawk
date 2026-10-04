#!/usr/bin/env python3
"""verify_install.py - is this the version of the game the results were measured on?

    py tools/verify_install.py --data "D:\\GOG\\F-117A"

Every parity and fidelity result in this repository was measured on one
version of the game: GOG's setup_f-117a_nighthawk_stealth_fighter_2.0_1.0_
(28044).exe, whose VGAME.EXE is MicroProse's final 473.04 update (the
installer ships it already applied; it is byte-identical to the VGAME.EXE
in "f11704 (473.04 Update).zip"). Verified 3 October 2026 by extracting the
installer (innoextract 1.9) and the patch and comparing byte for byte.

This checks a copy against the SHA-256 of every file the project reads:
the 17 code files the recompiler translates and the pilot roster the game
starts with; and, separately, the DOSBox the fidelity probe compares
against. Steam's release (build 425321, the game in its F-117A subfolder)
has the same game files, byte for byte; its DOSBox is a different build.
Hashes only; nothing of the game is in this file.
"""
from __future__ import annotations

import argparse
import hashlib
import os
import sys

# (path in the install, size, SHA-256), from the verified install.
EXPECTED = [
    ("F117.COM", 1722, "ce8f91618d4d25fa3c9236c53b9bb02ea27c15ee331cb8b8338877e7eabc7119"),
    ("SETUP.EXE", 8847, "b62a032582c114b6a522483f4291f3cd6dc491fa555e5dc57def5d49b987b040"),
    ("MPS_LOGO.EXE", 10686, "fc0f07a58bff0f9bfdaf2485e03cf0b9fb365e0063c55803c2e498ea4f77f76a"),
    ("PLAYER.EXE", 9506, "0f0e65725951785165f66299237585ee19ad98f490d7b882f6bf179217b07c49"),
    ("DSWAP.EXE", 8558, "87a678b93129b8ca7e8a87b3396d7da8b523b1e826f4ffb64cf23d7e526d0847"),
    ("START.EXE", 47100, "faa510b21c821ff37e0468104ba1cedf1b1d8d241541b38fc3a13fcadc73053d"),
    ("VGAME.EXE", 95979, "3371b07a8378c94962158dbb2bd575a4b361af1090924ff35551c2bf3cd95b2b"),
    ("END.EXE", 24203, "21f75e8abe03c3209022a18d42b6bec4f417206a2fb74a7fe7ff5404e8c4f7d8"),
    ("MGRAPHIC.EXE", 11950, "95ca0cd8161f370b87a8671495cb1323af117d174948c41a84006f4135aed566"),
    ("MISC.EXE", 689, "a44a3b783000123a4c0dbcf3eff6511498348bef4131dc5d36a9cb20597e36ef"),
    ("ASOUND.117", 14890, "b736d9196d53b31dd380bb312a8b478ee5a163eff8803e102a19148cc7aa8c8f"),
    ("ISOUND.117", 10226, "1a8a9ebc9ca6c6d8286a3e9641f01fc5ea13c29d8636009e224248b300d2f697"),
    ("RSOUND.117", 14628, "ebea14c6dd82f7c2b7ce0cddde5f2b2b0ceed4d5a739b22889c902ecbedfbba7"),
    ("NSOUND.117", 625, "0d6634508484f9ae6bf2413fa8c22ff7516090eb057ea27ace3ac494ed12cb0e"),
    ("ASOUND.LOG", 6120, "06db9ef02010a3e6850328f57633244b88ef18e527de8354287c2c66c5a05bba"),
    ("ISOUND.LOG", 2458, "ee6d184af60c4927046052d9a687cb9a0dacbcc99e2b14216103cb0d288d9c6e"),
    ("RSOUND.LOG", 5481, "ef76f7c20a603a2ea7c194c8c5aa091cdb47f468020040859293f39186a39352"),
    ("ROSTER.FIL", 802, "1977ab817899c3bc33f28a9ca5ee2f894e33ea7ae5f8d033e4c89b0be743bdb3"),
]

# GOG's DOSBox: the reference tools/fidelity.py runs. Not part of the game;
# Steam ships a different build (0.74) with different settings.
GOG_DOSBOX = [
    ("dosboxF117A.conf", 11445, "c6058802cac0249118bf885506dc55e38722853afbda354960c49cf9eeb0f899"),
    ("DOSBOX/DOSBox.exe", 3802624, "8a7a7fedd222bf985b51191000557f9ee2ffb033be02c6c59594dbbec013cd41"),
    ("DOSBOX/dosbox-0.74-2.1.tar.gz", 1334686, "1b6c865340e9d6119529c4ae61e2f12661424a38f07f8818202c5357a6b97ba2"),
]


def find_ci(root, rel):
    p = root
    for part in rel.split("/"):
        if not os.path.isdir(p):
            return None
        hit = [f for f in os.listdir(p) if f.lower() == part.lower()]
        if not hit:
            return None
        p = os.path.join(p, hit[0])
    return p


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--data", required=True, help="the game's install folder")
    a = ap.parse_args()
    def check(items):
        bad = 0
        for rel, size, digest in items:
            p = find_ci(a.data, rel)
            if not p:
                print("  MISSING    %s" % rel)
                bad += 1
                continue
            h = hashlib.sha256(open(p, "rb").read()).hexdigest()
            ok = h == digest and os.path.getsize(p) == size
            bad += not ok
            print("  %-10s %s" % ("ok" if ok else "DIFFERENT", rel))
        return bad
    print("The game:")
    bad = check(EXPECTED)
    print("%d of %d game files match the verified version" % (len(EXPECTED) - bad, len(EXPECTED)))
    print("GOG's DOSBox (the fidelity reference; optional):")
    dbad = check(GOG_DOSBOX)
    print("%d of %d match GOG's" % (len(GOG_DOSBOX) - dbad, len(GOG_DOSBOX)))
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
