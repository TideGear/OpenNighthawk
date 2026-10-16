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
"""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
# Presentation analysis tools: run by people and tests, never by the runner or the gate's checks.
TOOLS = {"tools/interp_frame.py", "tools/motion_check.py", "tools/present_diag.py", "tools/drawlist_frame.py",
         "tools/drawlist_spans.py", "tools/hires_frame.py", "tools/hires_spans.py", "tools/hires_subpixel.py"}
TESTS = re.compile(r"tests/test_(present_\w+|hud_\w+|band_\w+|world_\w+|map_marker|interp|hires|drawlist|"
                   r"near_polygons|nearclip|scene_mesh|frame_pacer)\.c$")
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
        elif TESTS.match(name) or name in TOOLS or name == "tests/test_interp_ident.py" or name.startswith("docs/") or (name.endswith(".md") and "/" not in name):
            continue
        else:
            out.append(f"{name}: not presentation-only")
    return out


def run(cmd, log):
    with open(log, "w") as stream:
        r = subprocess.run(cmd, cwd=ROOT, stdout=stream, stderr=subprocess.STDOUT)
    return r.returncode, Path(log).read_text(errors="replace")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--base", required=True, help="the commit the last full gate passed on")
    ap.add_argument("--data", required=True)
    ap.add_argument("--out", required=True, type=Path)
    ap.add_argument("--baseline-runs", type=Path, default=Path.home() / "f117-recomp-local" / "runs")
    ap.add_argument("--jobs", type=int, default=8)
    ap.add_argument("--list-only", action="store_true", help="only say whether the change qualifies")
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
    for label, extra in (("n1", ["--scales", "1"]), ("boot", ["--scales", "4", "9", "--routes", "boot_to_flight"])):
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
