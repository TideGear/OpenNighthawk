#!/usr/bin/env python3
"""The light check for a presentation-only change (docs/repeated-processes.md).

A change confined to read-only presentation code cannot change the emulated machine: the guest runs the
same instructions and the presentation only reads what the observer hands it. Such a change is
checked by every CTest, the 45-route N=1 presentation check (which also holds every guest checkpoint
and final hash to the last full gate's baseline runs) and boot at N=4/9, instead of the full gate.

This script refuses unless every file changed since BASE (the commit the last full gate passed on;
committed, staged, unstaged and untracked) is presentation-only: under src/present/ but not included
from outside src/present/ and src/host/ (the observer in src/matched/ includes some of its headers),
a presentation test, or documentation. Anything else needs the full gate. Run:
  py tools/present_light_check.py --base REV --data D:/GOG/F-117A --out DIR
A full gate is still needed before the next Phase 2 kit checkpoints: any change under src/ retires
the interpreter cache entry that `kit.py checkpoints` needs.
The draw feed and rerun implementation snapshot/execute machines and are not read-only presentation:
changes there require the full gate and the separate --mode rerun route checks.

App-only files (APP_ONLY: the window's main loop and the example presets) are built or copied only for
f117a.exe, never into f117run, the tests or anything the gate runs, so the gate cannot check them. A
change there needs --app-session (a recorded session: input.log and save-start/) and --app-exit-after:
the app replays it headless (dummy video and audio, software renderer) in interp and in rerun at
scale 3, and every logic frame must be exact, rerun must draw in-between pictures, and every palette
scanned out must also be presented, give or take the first and last frame (a stale palette fell short). The route
checks run only when presentation code changed, since they never run the app.
"""
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
# Presentation analysis tools: run by people and tests, never by the runner or the gate's checks.
TOOLS = {"tools/interp_frame.py", "tools/motion_check.py", "tools/present_diag.py", "tools/drawlist_frame.py",
         "tools/drawlist_spans.py", "tools/hires_frame.py", "tools/hires_spans.py", "tools/hires_subpixel.py",
         "tools/present_light_check.py"}
TESTS = re.compile(r"tests/test_(present_\w+|hud_\w+|band_\w+|world_\w+|map_marker|interp|hires|drawlist|"
                   r"near_polygons|nearclip|scene_mesh|frame_pacer)\.c$")
APP_ONLY = {"src/host/main.c", "f117a.example.ini", "f117a.4k.ini"}
MACHINE_EXECUTION = {"src/present/drawfeed.c", "src/present/drawfeed.h",
                     "src/present/rerun.c", "src/present/rerun.h"}


def git(*args):
    return subprocess.run(["git", *args], cwd=ROOT, capture_output=True, text=True, check=True).stdout


def changed_files(base):
    names = set(git("diff", "--name-only", base).split())
    # Untracked files count only where a build or test could pick them up (not the owner's own files).
    names |= {n for n in git("ls-files", "--others", "--exclude-standard").split()
              if n.startswith(("src/", "tests/", "tools/", "CMakeLists"))}
    return sorted(names)


def shared_present_headers():
    """Files under src/present/ that code outside src/present/ and src/host/ includes."""
    shared = set()
    for path in (ROOT / "src").rglob("*.[ch]"):
        rel = path.relative_to(ROOT).as_posix()
        if rel.startswith(("src/present/", "src/host/")):
            continue
        for m in re.finditer(r'#\s*include\s+"([^"]*present/[^"]+)"', path.read_text(errors="replace")):
            shared.add("src/present/" + m.group(1).rsplit("present/", 1)[1])
    return shared


def refusals(names):
    shared = shared_present_headers()
    out = []
    for name in names:
        if name.startswith("src/present/"):
            if name in MACHINE_EXECUTION:
                out.append(f"{name}: machine snapshot/execution code; requires full gate and rerun route checks")
            elif name in shared:
                out.append(f"{name}: included by the observer or other guest-side code")
        elif name in APP_ONLY:
            continue
        elif TESTS.match(name) or name in TOOLS or name == "tests/test_interp_ident.py" or name.startswith("docs/") or name.endswith(".md"):
            continue
        else:
            out.append(f"{name}: not presentation-only")
    return out


def run(cmd, log):
    with open(log, "w") as stream:
        r = subprocess.run(cmd, cwd=ROOT, stdout=stream, stderr=subprocess.STDOUT)
    return r.returncode, Path(log).read_text(errors="replace")


def app_smoke(a):
    """Replay a recorded session through f117a.exe in interp and rerun; return (ok, results)."""
    exe = ROOT / "build" / "f117a.exe"
    env = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy", SDL_RENDER_DRIVER="software")
    procs, results = [], {}
    for mode in ("interp", "rerun"):
        out = a.out / ("app-" + mode)
        if out.exists():
            shutil.rmtree(out)
        shutil.copytree(a.app_session / "save-start", out / "save")
        args = [str(exe), "--no-config", "--no-record", "--data", a.data, "--save", str(out / "save"),
                "--replay", str(a.app_session / "input.log"), "--exit-after", str(a.app_exit_after),
                "--roland", "off", "--present", mode, "--present-scale", "3", "--log", str(out / "app.log")]
        procs.append((mode, out, subprocess.Popen(args, cwd=ROOT, env=env, stdout=subprocess.DEVNULL,
                                                  stderr=subprocess.DEVNULL)))
    ok = True
    for mode, out, p in procs:
        rc = p.wait()
        log = (out / "app.log").read_text(errors="replace") if (out / "app.log").exists() else ""
        r = {"exit": rc}
        m = re.search(r"\[present\] replay: (\d+) logic frames, (\d+) equal to the display at their close, (\d+) not", log)
        if m:
            r.update(frames=int(m.group(1)), exact=int(m.group(2)), inexact=int(m.group(3)))
        m = re.search(r"\[present\] palettes: (\d+) distinct presented, (\d+) distinct scanned out", log)
        if m:
            r.update(palettes_presented=int(m.group(1)), palettes_scanned=int(m.group(2)))
        m = re.search(r"\[present\] rerun: (\d+) in-between pictures", log)
        if m:
            r["rerun_drawn"] = int(m.group(1))
        errors = []
        if rc:
            errors.append("exit code %d" % rc)
        if not r.get("frames") or r.get("inexact") or r.get("exact") != r.get("frames"):
            errors.append("logic frames missing or not all exact")
        # Each presented palette is the one scanned out a host frame earlier, so the two sets differ by at most
        # the first and last frame; the frozen palette (fixed 15 Oct) presented 186 against 209.
        if "palettes_presented" not in r or r["palettes_presented"] < r["palettes_scanned"] - 2:
            errors.append("presented palettes fall short of the scanned-out ones (a stale palette)")
        if mode == "rerun" and not r.get("rerun_drawn"):
            errors.append("no in-between pictures drawn by the original")
        r["errors"] = errors
        results[mode] = r
        ok = ok and not errors
        print("  app %s: %s" % (mode, json.dumps(r)))
    return ok, results


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--base", required=True, help="the commit the last full gate passed on")
    ap.add_argument("--data", required=True)
    ap.add_argument("--out", required=True, type=Path)
    ap.add_argument("--baseline-runs", type=Path, default=Path.home() / "f117-recomp-local" / "runs")
    ap.add_argument("--jobs", type=int, default=8)
    ap.add_argument("--list-only", action="store_true", help="only say whether the change qualifies")
    ap.add_argument("--app-session", type=Path, help="a recorded session (input.log, save-start/) for app-only changes")
    ap.add_argument("--app-exit-after", type=int, help="machine clock at which the app replay stops")
    a = ap.parse_args()
    names = changed_files(a.base)
    bad = refusals(names)
    for name in names:
        print("  changed " + name)
    if bad:
        for line in bad:
            print("  REFUSED " + line)
        print("not presentation-only: run the full gate (tools/build_recomp.py)")
        return 1
    print("presentation-only: %d file(s) changed since %s" % (len(names), a.base))
    app_changed = any(n in APP_ONLY for n in names)
    present_changed = any(n not in APP_ONLY and n != "tools/present_light_check.py" and not n.startswith("docs/")
                          and not n.endswith(".md") for n in names)
    if app_changed and not (a.app_session and a.app_exit_after):
        print("refused: app-only files changed; give --app-session and --app-exit-after")
        return 1
    if a.list_only:
        return 0
    a.out.mkdir(parents=True, exist_ok=True)
    env_note = "SDL_VIDEODRIVER/SDL_AUDIODRIVER must be dummy"
    if os.environ.get("SDL_VIDEODRIVER") != "dummy":
        print("refused: " + env_note + " (no window on the owner's screen)")
        return 2
    verdict = {"base": a.base, "files": names}
    rc, text = run(["cmd", "/c", str(ROOT / "build.cmd"), "-DF117R_BUILD_APP=ON"], a.out / "build.log")
    if rc or "BUILD OK" not in text[-2000:]:
        print("build failed; see " + str(a.out / "build.log"))
        return 1
    rc, text = run(["ctest", "--test-dir", str(ROOT / "build"), "-j", str(a.jobs), "--timeout", "1800"], a.out / "ctest.log")
    tally = re.search(r"(\d+)% tests passed, (\d+) tests failed out of (\d+)", text)
    verdict["ctest"] = tally.group(0) if tally else None
    print("  ctest: " + (verdict["ctest"] or "no tally"))
    if rc or not tally or tally.group(2) != "0":
        return 1
    if app_changed:
        ok, verdict["app"] = app_smoke(a)
        if not ok:
            return 1
    for label, extra in ((("n1", ["--scales", "1"]), ("boot", ["--scales", "4", "9", "--routes", "boot_to_flight"]))
                         if present_changed else ()):
        out = a.out / label
        rc, text = run([sys.executable, str(ROOT / "tools/presentation_check.py"), "--data", a.data,
                        "--baseline-runs", str(a.baseline_runs), "--out", str(out), "--jobs", str(a.jobs)] + extra,
                       a.out / (label + ".log"))
        report = json.loads((out / "verdict.json").read_text()) if (out / "verdict.json").exists() else None
        failed = [r for r in report["results"] if r["errors"]] if report else None
        frames = sum(r.get("frames", 0) for r in report["results"]) if report else 0
        pairs = sum(r.get("pairs", 0) for r in report["results"]) if report else 0
        verdict[label] = dict(results=len(report["results"]) if report else 0, failed=len(failed or []),
                              frames=frames, pairs=pairs, runner_changed=report and report["runner_changed"])
        print("  %s: %d checks, %d failed, %d frames / %d pairs" % (label, verdict[label]["results"],
                                                                    verdict[label]["failed"], frames, pairs))
        if rc or failed is None or failed or report["runner_changed"]:
            return 1
    (a.out / "light-verdict.json").write_text(json.dumps(verdict, indent=2) + "\n")
    print("light check passed. A full gate is still needed before the next Phase 2 kit checkpoints.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
