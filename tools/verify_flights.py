#!/usr/bin/env python3
"""Hold the default fix set against the original on recorded flights (docs/flight-reference.json).

Two checks per recorded session, both from replays of the session's own log:

  state    the full fix set's replay, dumped at a clock, must show the values GOG's own DOSBox shows for the
           same log (for example the frame's placed-object count): a fix that hides something the original
           draws (the Q11 jets) changes them.
  effects  replaying with each fix removed in turn, the fixes that change the flight's final machine hash
           must be exactly the reviewed list: a fix that starts changing a flight, or stops, fails until the
           list is reviewed against the original (tools/fix_audit.py, docs/bugs.md "Fix audit").

Run it when fixes change (src/fixes, a fix table, a new fix) and before a release; it is not in the default
gate (about 25 minutes at 4 jobs for two sessions). The sessions hold game data, so they are read from a
local folder, never from the repository; a missing one is skipped.

    py tools/verify_flights.py --app build/f117a.exe --data D:/GOG/F-117A --sessions SESSIONS_DIR --out OUT_DIR [--jobs 4]
"""
import argparse
import concurrent.futures as cf
import json
import re
import struct
import sys
from pathlib import Path
from types import SimpleNamespace

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import fix_audit                      # noqa: E402
from dosbox_flight import find_psp    # noqa: E402


def ds_word(ram, offset):
    psp = find_psp(ram, b"VGAME")
    if psp is None:
        return None
    ds = (psp + 0x10 + 0x1E42) << 4
    return struct.unpack_from("<H", ram, ds + offset)[0]


def check_session(args, ref):
    sdir = args.sessions / ref["id"]
    if not (sdir / "input.log").exists():
        print("%s: not in %s, skipped" % (ref["id"], args.sessions))
        return 0
    log = (sdir / "input.log").read_text(errors="replace")
    ids = fix_audit.header_fixes(sdir / "input.log")
    fa = SimpleNamespace(app=args.app, data=args.data, session=sdir, out=args.out / ref["id"],
                         exit_after=ref["exit_after"], timeout=args.timeout, extra_env={})
    fa.out.mkdir(parents=True, exist_ok=True)
    dumps = {}
    for st in ref["state"]:
        dumps.setdefault(st["clock"], fa.out / ("ram-%d.bin" % st["clock"]))
    for clock, path in dumps.items():          # one dump per clock, from the full-set run
        fa.extra_env["all"] = {"F117_DUMP_RAM": "%d:%s" % (clock, path)}
    jobs = {"all": log}
    for fid in ids:
        jobs["no-" + fid] = re.sub(r"^(# f117r-fixes.*?)(?<=\s)%s(\s|$)" % re.escape(fid), r"\1\2", log, count=1, flags=re.M)
    with cf.ThreadPoolExecutor(args.jobs) as pool:
        futs = {k: pool.submit(fix_audit.run, fa, k, v) for k, v in jobs.items()}
        res = {k: f.result() for k, f in futs.items()}
    bad = 0
    base = res["all"][0]
    if base is None:
        print("%s: the full-set replay did not finish" % ref["id"])
        return 1
    for st in ref["state"]:
        ram = dumps[st["clock"]].read_bytes() if dumps[st["clock"]].exists() else b""
        got = ds_word(ram, int(st["ds_offset"], 16)) if ram else None
        ok = got == st["expect"]
        print("%s state %-5s %s: %s, expected %s%s" % (ref["id"], st["ds_offset"], st["what"][:48], got, st["expect"], "" if ok else "   <-- DIFFERS FROM THE ORIGINAL"))
        bad += not ok
    changed = sorted(fid for fid in ids if res["no-" + fid][0] not in (None, base))
    failed = sorted(fid for fid in ids if res["no-" + fid][0] is None)
    want = sorted(ref["effects"])
    print("%s effects: %s" % (ref["id"], " ".join(changed) or "none"))
    if failed:
        print("  replays that did not finish: %s" % " ".join(failed))
        bad += 1
    if changed != want:
        print("  <-- NOT THE REVIEWED LIST (%s): new %s, gone %s. Review against the original, then update docs/flight-reference.json"
              % (" ".join(want), " ".join(sorted(set(changed) - set(want))) or "-", " ".join(sorted(set(want) - set(changed))) or "-"))
        bad += 1
    return bad


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--app", type=Path, required=True)
    ap.add_argument("--data", type=Path, required=True)
    ap.add_argument("--sessions", type=Path, required=True)
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument("--reference", type=Path, default=HERE.parent / "docs" / "flight-reference.json")
    ap.add_argument("--jobs", type=int, default=4)
    ap.add_argument("--timeout", type=int, default=3000)
    args = ap.parse_args()
    ref = json.loads(args.reference.read_text(encoding="utf-8"))
    bad = sum(check_session(args, s) for s in ref["sessions"])
    print("flights: %s" % ("all as reviewed" if not bad else "%d problem(s)" % bad))
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
