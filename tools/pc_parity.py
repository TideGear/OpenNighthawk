#!/usr/bin/env python3
"""pc_parity.py - this machine's PC model held to three reference machines.

    py tools/pc_parity.py --data GOG_DIR --dosbox-x PATH\\dosbox-x.exe [--seconds 130]
                          [--gog-reference RUN_DIR] [--no-86box] [--no-dosbox-x] [--no-gog]

The model (interpreter and recompiled code run on the same PC) cannot show an
error in itself, so it is checked against machines it did not come from. The
intro is played on each and its pictures compared with this machine's:

  GOG DOSBox 0.74   the saved ZMBV capture (--gog-reference, default the
                    reference run recorded 5 Oct); no DOSBox is started.
  DOSBox-X          the patched build (tools/ref86box/build_dosbox_x.md),
                    run headless; video_compare.py reads its capture.
  86Box             the VNC build (tools/ref86box/build_86box.md), headless;
                    capture_intro.py and compare_intro.py.

The three run at once (none shows a window or makes a sound); with each,
the intro's music is checked too (sound_parity.py on the DOSBoxes' captured audio,
ref86box/compare_opl86.py on 86Box's AdLib writes), and a scripted START session's
saved roster is compared byte for byte (save_parity.py, on DOSBox-X and 86Box). Each verdict is
against the figures measured on 6 Oct 2026, so a change that makes this machine
drift from a reference fails here; the thresholds are at the top of the file
and say what was measured. Exit status is 0 when every enabled check passes.
"""
import argparse
import json
import os
import re
import subprocess
import sys
import threading
from pathlib import Path

HERE = Path(__file__).resolve().parent
PY = sys.executable
DEFAULT_GOG_REFERENCE = Path.home() / "f117-recomp-local" / "video" / "intro-qlh_wi26"
DEFAULT_GOG_MUSIC = Path.home() / "f117-recomp-local" / "video" / "gog-music" / "mps_logo_000.dro"   # GOG's raw OPL capture of the intro

# Measured 6 Oct 2026 (see docs/repeated-processes.md and build_dosbox_x.md).
LIMITS = {
    # exact pictures in order; unmatched pictures lasting more than one sample;
    # largest timing drift in ms
    "gog": dict(min_exact=1300, max_multi_unmatched=0, max_drift_ms=100),   # measured 1329, 0, 57 ms
    "dosbox-x": dict(min_exact=1200, max_multi_unmatched=3, max_drift_ms=350),   # measured 1237, 3, 143-200 ms (real-time captures varied; fast-forward ones are identical)
    # 86Box: every graphics picture matches ours exactly in 6-bit DAC values, in order, except the
    # ones named in ref86box/expected_misses86.txt (each tied to its picture's hash). Measured 8 Oct 2026:
    # 86 of 87 exact, the other a single sample taken mid-draw. min_total guards against a comparison
    # that found nothing to compare.
    "86box": dict(min_total=80),
    # GOG's raw OPL capture: every write, from this machine started 275 ms in (dosbox_compare.GOG_BOOT_MS); measured 22,840, -14..+1 ms
    "gog-music": dict(min_writes=22800, max_timing_ms=40),
}


def judge_86box_pictures(text, expected_text, min_total):
    """compare_intro.py's report against the reviewed list of expected misses: (ok, summary, stale).
    Every picture that is not exact must be on the list with its current hash, none may be unmatched or
    out of order, and the counts must add up (a picture the report does not name cannot slip through)."""
    m = re.search(r"graphics pictures (\d+): exact (\d+), close (\d+), unmatched (\d+); backwards matches (\d+)", text)
    if not m:
        return False, "ERROR no comparison result", []
    total, exact, close, unmatched, back = map(int, m.groups())
    expected = {}
    for line in expected_text.splitlines():
        if line.strip() and not line.startswith("#"):
            name, digest = line.split()[:2]
            expected[name] = digest
    seen = dict(re.findall(r"^\s+(p\d+\.png) close .*?hash (\w+)", text, re.M))
    unlisted = sorted(n for n, h in seen.items() if expected.get(n) != h)
    stale = sorted(n for n in expected if n not in seen)
    ok = (total >= min_total and unmatched == 0 and back == 0 and not unlisted
          and len(seen) == close and exact + close == total)
    summary = "%d pictures: %d exact, %d close (on the reviewed list: %s), %d unmatched, %d out of order" % (
        total, exact, close, "all" if not unlisted else "NOT " + ", ".join(unlisted), unmatched, back)
    return ok, summary, stale


def run(cmd, log):
    with open(log, "w") as f:
        return subprocess.run(cmd, stdout=f, stderr=subprocess.STDOUT, text=True).returncode


def video_compare(args, log, extra):
    rc = run([PY, str(HERE / "video_compare.py"), "--data", args.data, "--seconds", str(args.seconds),
              "--work-dir", str(args.out / "video")] + extra, log)
    text = Path(log).read_text(errors="replace")
    art = re.search(r"Artifacts: (.+)", text)
    exact = re.search(r"(\d+) exact RGB pictures", text)
    multi = re.search(r"unmatched lasting multiple samples: DOSBox (\d+), here (\d+)", text)
    drift = re.search(r"timing drift \(ms\): min ([-+\d.]+), max ([-+\d.]+), end ([-+\d.]+)", text)
    return dict(rc=rc, run=art and art.group(1).strip(),
                exact=int(exact[1]) if exact else 0,
                multi=(int(multi[1]) + int(multi[2])) if multi else 99,
                drift=max(abs(float(drift[1])), abs(float(drift[2]))) if drift else 1e9, text=text)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--data", required=True)
    ap.add_argument("--dosbox-x", type=Path, default=Path(r"D:\86box-src\dbx-src\src\dosbox-x.exe"))
    ap.add_argument("--seconds", type=int, default=130)
    ap.add_argument("--gog-reference", type=Path, default=DEFAULT_GOG_REFERENCE)
    ap.add_argument("--gog-music", type=Path, default=DEFAULT_GOG_MUSIC, help="GOG DOSBox raw OPL capture (.dro) of the intro")
    ap.add_argument("--out", type=Path, default=Path.home() / "f117-recomp-local" / "pc-parity")
    for name in ("gog", "dosbox-x", "86box", "save"):
        ap.add_argument("--no-" + name, action="store_true")
    a = ap.parse_args()
    a.out.mkdir(parents=True, exist_ok=True)
    results, threads = {}, []

    def job(name, fn):
        def go():
            try:
                results[name] = fn()
            except Exception as e:                                  # a failed reference is a failed check
                results[name] = dict(error=str(e))
        t = threading.Thread(target=go)
        t.start()
        threads.append(t)

    def save_check(extra, log, label):
        """A scripted START session's saved ROSTER.FIL against ours (save_parity.py)."""
        rc = run([PY, str(HERE / "save_parity.py"), "--data", a.data, "--dosbox-x", str(a.dosbox_x)] + extra, log)
        line = [l for l in Path(log).read_text(errors="replace").splitlines() if l.startswith("save ")]
        return dict(rc=rc, line=line[-1] if line else "save %-10s %-8s ERROR see %s" % ("ROSTER.FIL", label, log))

    if not a.no_dosbox_x and not a.no_save:
        # at DOSBox-X's own speed: in fast-forward its scripted clicks miss (the roster came out with a
        # pilot not erased, 61 bytes off, and START once waited 25 minutes), so the check takes 4 minutes
        job("save-dbx", lambda: save_check(["--no-86box", "--no-turbo", "--out", str(a.out / "save-dosbox-x")], a.out / "dosbox-x-save.log", "DOSBox-X"))
    if not a.no_dosbox_x:
        job("dosbox-x", lambda: video_compare(a, a.out / "dosbox-x.log", ["--dosbox", str(a.dosbox_x)]))
    if not a.no_gog and a.gog_music.is_file():
        job("gog-music", lambda: dict(rc=run([PY, str(HERE / "dosbox_compare.py"), "--data", a.data, "--dro", str(a.gog_music),
                                              "--seconds", "100"], a.out / "gog-music.log")))
    if not a.no_gog:
        job("gog", lambda: video_compare(a, a.out / "gog.log", ["--against", str(a.gog_reference)]))
    if not a.no_86box:
        def box():
            cap = a.out / "86box"
            # three independent 86Box runs at once: each starts and stops only its own process (its
            # profile is in its command line); all three are traced, open no VNC port and run on
            # emulated time in fast-forward, so load cannot change their results
            out = {}

            def one(key, fn):
                def go():
                    try:
                        out[key] = fn()
                    except Exception as e:
                        out[key] = e
                t = threading.Thread(target=go)
                t.start()
                return t
            runs = [
                one("rc", lambda: run([PY, str(HERE / "ref86box" / "capture_intro.py"), str(cap), "--seconds", str(a.seconds)],
                                      a.out / "86box-capture.log")),
                one("sound_rc", lambda: run([PY, str(HERE / "ref86box" / "sound86.py"), str(a.out / "86box-sound")],
                                            a.out / "86box-sound-run.log")),
            ]
            if not a.no_save:
                runs.append(one("save", lambda: save_check(["--no-dosbox-x", "--out", str(a.out / "save-86box")],
                                                           a.out / "86box-save.log", "86Box")))
            for t in runs:
                t.join()
            for v in out.values():
                if isinstance(v, Exception):
                    raise v
            return dict(rc=out["rc"], cap=str(cap), sound_rc=out["sound_rc"], opl=str(a.out / "86box-sound" / "opl86.log"),
                        save=out.get("save"))
        job("86box", box)
    for t in threads:
        t.join()

    failures = []
    print("PC parity, intro, %d s" % a.seconds)
    for name in ("gog", "dosbox-x"):
        if name not in results:
            continue
        r, lim = results[name], LIMITS[name]
        if "error" in r or r["rc"] not in (0, 1) or not r["run"]:
            failures.append(name)
            print("  %-9s ERROR %s" % (name, r.get("error", "no result; see %s" % (a.out / (name + ".log")))))
            continue
        # video_compare exits 1 whenever any picture is unmatched; the figures are the verdict.
        ok = r["exact"] >= lim["min_exact"] and r["multi"] <= lim["max_multi_unmatched"] and r["drift"] <= lim["max_drift_ms"]
        print("  %-9s %s  %d exact pictures (need %d), %d multi-sample unmatched (max %d), drift %.0f ms (max %d)" % (
            name, "PASS" if ok else "FAIL", r["exact"], lim["min_exact"], r["multi"], lim["max_multi_unmatched"],
            r["drift"], lim["max_drift_ms"]))
        if not ok:
            failures.append(name)
        # the same run's AdLib log, rendered by the application, against the capture's sound
        avi = (a.gog_reference if name == "gog" else Path(r["run"])) / "capture" / "mps_logo_000.avi"
        slog = a.out / (name + "-sound.log")
        src = run([PY, str(HERE / "sound_parity.py"), "--reference", name, "--avi", str(avi),
                   "--opl-log", str(Path(r["run"]) / "opl.log")], slog)
        line = [l for l in Path(slog).read_text(errors="replace").splitlines() if l.startswith("sound ")]
        print("  " + (line[0] if line else "sound %-8s ERROR see %s" % (name, slog)))
        if src != 0:
            failures.append(name + " sound")
    if "gog-music" in results:
        text = (a.out / "gog-music.log").read_text(errors="replace")
        m = re.search(r"(\d+) writes in the same order with the same values, over ([\d.]+) s", text)
        t = re.search(r"in ms: at the end ([+-]\d+), smallest ([+-]\d+), largest ([+-]\d+)", text)
        same = "no difference until one sequence ended (DOSBox 0 more, here 0 more)" in text
        ok = bool(m and t and same and int(m[1]) >= LIMITS["gog-music"]["min_writes"]
                  and max(abs(int(t[2])), abs(int(t[3]))) <= LIMITS["gog-music"]["max_timing_ms"])
        print("  music gog PASS  %s AdLib writes identical in order and value over %s s, timing %s to %s ms (need %d writes, within %d ms)" % (
            m[1], m[2], t[2], t[3], LIMITS["gog-music"]["min_writes"], LIMITS["gog-music"]["max_timing_ms"]) if ok else
            "  music gog FAIL  see %s" % (a.out / "gog-music.log"))
        if not ok:
            failures.append("gog music")
    if "86box" in results:
        r = results["86box"]
        shots = None
        if "dosbox-x" in results and results["dosbox-x"].get("run"):
            shots = Path(results["dosbox-x"]["run"]) / "shots"
        elif "gog" in results and results["gog"].get("run"):
            shots = Path(results["gog"]["run"]) / "shots"
        if "error" in r or r["rc"] != 0 or shots is None:
            failures.append("86box")
            print("  86box     ERROR capture or comparison pictures missing (%s)" % (r.get("error") or r.get("rc")))
        else:
            log = a.out / "86box-compare.log"
            rc = run([PY, str(HERE / "ref86box" / "compare_intro.py"), r["cap"], str(shots)], log)
            text = Path(log).read_text(errors="replace")
            m = re.search(r"graphics pictures (\d+): exact (\d+), close (\d+), unmatched (\d+); backwards matches (\d+)", text)
            if not m:
                failures.append("86box")
                print("  86box     ERROR no comparison result; see %s" % log)
            else:
                ok, summary, stale = judge_86box_pictures(
                    text, (HERE / "ref86box" / "expected_misses86.txt").read_text(encoding="utf-8"),
                    LIMITS["86box"]["min_total"])
                print("  86box     %s  %s" % ("PASS" if ok else "FAIL", summary))
                if stale:
                    print("            listed but now exact (remove from expected_misses86.txt): %s" % ", ".join(stale))
                if not ok:
                    failures.append("86box")
        ours_log = next((Path(results[n]["run"]) / "opl.log" for n in ("dosbox-x", "gog")
                         if n in results and results[n].get("run")), None)
        if r.get("sound_rc") != 0 or ours_log is None or not Path(r["opl"]).exists():
            failures.append("86box sound")
            print("  sound 86box    ERROR no AdLib log (sound86.py rc %s, see %s)" % (r.get("sound_rc"), a.out / "86box-sound-run.log"))
        else:
            slog = a.out / "86box-sound.log"
            src = run([PY, str(HERE / "ref86box" / "compare_opl86.py"), str(ours_log), r["opl"]], slog)
            print("  " + (Path(slog).read_text(errors="replace").strip() or "sound 86box ERROR"))
            if src != 0:
                failures.append("86box sound")
        ours_run = next((Path(results[n]["run"]) for n in ("dosbox-x", "gog") if n in results and results[n].get("run")), None)
        if ours_run is not None and (a.out / "86box-sound" / "trace" / "frames.csv").exists():
            tlog = a.out / "86box-timing.log"
            trc = run([PY, str(HERE / "ref86box" / "compare_timing86.py"), str(ours_run / "comparison.json"),
                       str(a.out / "86box-sound" / "trace" / "frames.csv")], tlog)
            print("  " + (Path(tlog).read_text(errors="replace").strip() or "timing 86box ERROR"))
            if trc != 0:
                failures.append("86box timing")
    saves = [("save-dbx", results.get("save-dbx")), ("86box save", results.get("86box", {}).get("save"))]
    for name, r in saves:
        if not r:
            continue
        print("  " + (r.get("line") or r.get("error", "save ERROR")))
        if r.get("rc") != 0:
            failures.append(name)
    print("RESULT:", "all checks passed" if not failures else "FAILED: " + ", ".join(failures))
    (a.out / "summary.json").write_text(json.dumps(
        {k: {kk: vv for kk, vv in v.items() if kk != "text"} for k, v in results.items()}, indent=1))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
