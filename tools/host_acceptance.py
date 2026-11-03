#!/usr/bin/env python3
"""Recorded-flight host integration checks. See docs/host-acceptance.md.

Run against a built f117a, never against the runner. Inputs and game data stay
local; every process gets private saves. Missing evidence is a failure.
"""
import argparse
import concurrent.futures
import hashlib
import json
import math
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import time
from datetime import datetime, timezone

from PIL import Image
import settings_acceptance

ROOT = Path(__file__).resolve().parents[1]


def require(condition, message):
    if not condition:
        raise ValueError(message)


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def verify_report(path, app, manifest_path=ROOT / "docs/host-acceptance.json"):
    """A partial, stale or incomplete report is not release/deployment evidence."""
    report = json.loads(Path(path).read_text())
    manifest = json.loads(Path(manifest_path).read_text())
    expected = {"sound", "input-live", "input-replay", "input-watch", "menu", "renderer-workers", "settings"}
    expected.update(s["name"] for s in manifest["render"])
    require(report.get("status") == "pass" and report.get("full_suite") is True and report.get("scope") == "all",
            "a complete passing host suite is required")
    require(report.get("app_sha256") == digest(app), "host report is for a different executable")
    require(report.get("manifest_sha256") == digest(manifest_path) and report.get("checker_sha256") == digest(__file__),
            "host report predates the current manifest/checker")
    require(report.get("settings_checker_sha256")==digest(settings_acceptance.__file__),"host report predates the settings checker")
    cases = report.get("cases", [])
    require(len(cases) == len(expected) and {c["name"] for c in cases} == expected and
            all(c.get("status") == "pass" for c in cases), "host report is missing required passing cases")
    require(report.get("recordings") == manifest["sessions"], "host report used different recordings")
    require(report.get("machine_parity") == "not_run" and report.get("owner_reference_comparison") == "pending",
            "host evidence must not claim machine parity or owner/GOG acceptance")
    return report


def clean_environment():
    # Developer diagnostics can change inputs or pictures. Never inherit them.
    env = {k: v for k, v in os.environ.items() if not k.startswith("F117_")}
    env.update(SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy",
               SDL_RENDER_DRIVER="software", F117_WINDOW="1920x1080",
               F117_PRESENT_THREADS_OFF="1")
    return env


def derived_input(source, destination, spec):
    comments, events = [], []
    for line in source.read_text().splitlines():
        if not line or line.startswith("#"):
            comments.append(line)
        else:
            clock = int(line.split()[1])
            if clock <= spec["keep_until"]:
                events.append((clock, line))
    events += [(clock, f"K {clock} {key}") for clock, key in spec["inject_keys"]]
    events += [(event[0], "J " + " ".join(map(str, event))) for event in spec.get("inject_joystick", [])]
    # Stable sorting preserves the recorded order of events sharing a clock.
    destination.write_text("\n".join(comments + [line for _, line in sorted(events, key=lambda e: e[0])]) + "\n")


def audio_window(chunks, lo, hi, field, rate, ips):
    first, last = lo * rate // ips, hi * rate // ips
    covered = audible = energy = 0.0
    previous = first
    for a in chunks:
        start, end = a["first"], a["first"] + a["frames"]
        overlap = min(end, last) - max(start, first)
        if overlap <= 0:
            continue
        require(a["side_frames"] == a["frames"] and a["side_first"] == start,
                "sidecar PCM is missing or not aligned with the final mixer")
        require(max(start, first) >= previous, "overlapping audio evidence")
        previous = min(end, last)
        rms = math.sqrt(a[field] / (2 * a["frames"]))
        covered += overlap
        audible += overlap if rms >= 8 else 0
        energy += a[field] * overlap / a["frames"]
    require(last > first and covered >= (last - first) * 0.98, "audio evidence does not cover the requested window")
    return dict(coverage=covered / (last - first), audible_fraction=audible / covered,
                rms=math.sqrt(energy / (2 * covered)))


def check_sound(path, spec):
    requests, chunks, opl, fmt = [], [], [], None
    with path.open() as stream:
        for line in stream:
            a = json.loads(line)
            if a["kind"] == "format":
                require(fmt is None, "duplicate audio format record")
                fmt = a
            elif a["kind"] == "request":
                require(a["ok"] == 1, "sidecar request failed")
                requests.append(a)
            elif a["kind"] == "audio":
                require(a["frames"] > 0 and a["side_energy"] >= 0 and a["mix_energy"] >= 0, "invalid PCM energy record")
                chunks.append(a)
            elif a["kind"] == "opl":
                opl.append(a)
            else:
                raise ValueError("unknown host evidence record")
    require(fmt and fmt["version"] == 1 and fmt["sample_rate"] == 44100 and fmt["ips"] > 0,
            "missing or unsupported host audio evidence")
    rate, ips = fmt["sample_rate"], fmt["ips"]
    require(any(a["id"] == 24 for a in requests), "engine start never reached the sidecar")
    speeds = [a for a in requests if a["id"] == 23]
    require(speeds, "continuous speed updates never reached the sidecar")
    require(any(b["value"] >= a["value"] + 5 for a, b in zip(speeds, speeds[1:])) or
            any(a["value"] >= speeds[0]["value"] + 5 for a in speeds), "no engine acceleration was exercised")
    lo, hi = spec["coast"]
    coast = [a for a in speeds if lo <= a["clock"] <= hi]
    require(len(coast) >= spec["minimum_coast_updates"], "too few speed updates during zero-thrust coasting")
    require(all(a["thrust"] == 0 and a["value"] > 0 and a["value"] == a["observed_knots"] for a in coast),
            "coasting speed parameter was dropped, altered, or not observed at zero thrust")
    times = [a["clock"] for a in coast]
    require(times == sorted(set(times)), "duplicate or unordered speed forwarding")
    require(max(b - a for a, b in zip(times, times[1:])) <= ips / 5,
            "continuous speed forwarding has a gap")
    require(not any(a["id"] == 25 and lo <= a["clock"] <= hi for a in requests),
            "coast fixture contains an explicit engine-off command")
    result = {"speed_updates": len(speeds), "coast_updates": len(coast)}
    pitch = {a["value"] for a in opl if lo <= a["clock"] <= hi and a["reg"] == spec["engine_pitch_register"]}
    require(len(pitch) >= spec["minimum_pitch_values"], "engine pitch did not follow changing coasting speed")
    result["coast_pitch_values"] = sorted(pitch)
    start = next((a["clock"] for a in speeds if a["value"] >= 5 and a["thrust"] > 0), None)
    require(start is not None, "no moving engine-start interval")
    result["startup_pcm"] = audio_window(chunks, start + ips//4, start + ips*5//4, "side_energy", rate, ips)
    require(result["startup_pcm"]["rms"] >= spec["minimum_rms"], "engine startup was silent")
    for field in ("side_energy", "mix_energy"):
        w = audio_window(chunks, lo, hi, field, rate, ips)
        require(w["rms"] >= spec["minimum_rms"] and w["audible_fraction"] >= 0.95,
                f"engine PCM is silent or drops out during coasting ({field})")
        result[field] = w
    lo, hi = spec["overlap"]
    require(any(a["id"] == spec["effect_id"] and lo <= a["clock"] <= hi for a in requests),
            "injected control never produced the expected routed effect")
    require(any(a["value"] > 0 and lo <= a["clock"] <= hi for a in speeds), "effect was not exercised while moving")
    require(any(a["reg"] == spec["effect_keyon_register"] and a["value"] & 32 and lo <= a["clock"] <= hi for a in opl),
            "routed effect never keyed on its non-engine OPL voice")
    result["effect_pcm"] = audio_window(chunks, lo, hi, "mix_energy", rate, ips)
    require(result["effect_pcm"]["rms"] >= spec["minimum_rms"], "overlapping effect produced no mixed PCM")
    lo, hi = spec["stop"]
    stopped = [a for a in speeds if lo <= a["clock"] <= hi]
    require(len(stopped) >= 15 and all(a["value"] == 0 and a["thrust"] == 0 for a in stopped),
            "braking fixture did not reach a sustained stationary idle")
    result["stopped_pcm"] = audio_window(chunks, lo, hi, "side_energy", rate, ips)
    require(result["stopped_pcm"]["rms"] <= spec["maximum_stopped_rms"], "engine PCM continued after speed reached zero")
    return result


def component_areas(im, predicate):
    pixels = {(x, y) for y in range(im.height) for x in range(im.width) if predicate(im.getpixel((x, y)))}
    areas = []
    while pixels:
        todo = [pixels.pop()]
        area = 0
        while todo:
            x, y = todo.pop()
            area += 1
            for q in ((x-1, y), (x+1, y), (x, y-1), (x, y+1)):
                if q in pixels:
                    pixels.remove(q)
                    todo.append(q)
        areas.append(area)
    return areas


def feature_ok(im, feature):
    roi = feature["roi"]
    require(0 <= roi[0] < roi[2] <= im.width and 0 <= roi[1] < roi[3] <= im.height, "feature region is outside the picture")
    im = im.crop(roi).convert("RGB")
    kind = feature["kind"]
    if kind in ("cream_component", "flag_component"):
        predicate = (lambda p: p == (255, 255, 170)) if kind == "cream_component" else (
            lambda p: p[0] > p[1] + 8 and p[0] > p[2] + 8 and p[0] < 100 and p[1] < 40 and p[2] < 40)
        return max(component_areas(im, predicate), default=0) >= feature["minimum_area"]
    pixels = list(im.getdata())
    if kind == "no_yellow":
        return not any(r >= 100 and g >= 80 and b < 50 for r, g, b in pixels)
    if kind == "water_fraction":
        return sum(r < 50 and b >= g and b > r + 24 for r, g, b in pixels) / len(pixels) >= feature["minimum"]
    if kind == "ground_fraction":
        return sum(r >= 32 and b < 96 and r >= b * 1.15 and g >= b * 1.1 and 0 <= r-g <= 32
                   for r, g, b in pixels) / len(pixels) >= feature["minimum"]
    if kind == "dark_fraction":
        return sum(max(p) <= 32 for p in pixels) / len(pixels) >= feature["minimum"]
    raise ValueError(f"unknown renderer feature: {kind}")


def check_motion(files, spec):
    require(len(files) >= 12, "too few moving frames")
    clocks = [int(p.stem.rsplit("-", 1)[-1]) for p in files]
    require(clocks == sorted(set(clocks)) and clocks[0] >= spec["clock"], "frame clocks are not ordered or sought correctly")
    require(clocks[-1] - clocks[0] >= spec["duration"] * 0.6, "captures do not span the moving scenario")
    hashes, frames = set(), []
    for file in files:
        with Image.open(file) as image:
            require(image.size == (1280, 600), "unexpected renderer resolution")
            im = image.convert("RGB")
            hashes.add(hashlib.sha256(im.crop((0, 90, 1280, 260)).tobytes()).hexdigest())
            frames.append(im)
    require(len(hashes) >= 3, "moving sequence is frozen")
    results = []
    for feature in spec["features"]:
        selected = frames[:1] if feature.get("first_only") else frames
        fraction = sum(feature_ok(im, feature) for im in selected) / len(selected)
        require(fraction >= feature["fraction"], f"{feature['kind']} failed: {fraction:.1%} frames, need {feature['fraction']:.1%}")
        results.append(dict(kind=feature["kind"], fraction=fraction, frames_checked=len(selected), roi=feature["roi"]))
    return {"frames": len(frames), "distinct_scene_frames": len(hashes), "clock_span": clocks[-1] - clocks[0], "features": results}


def check_workers(log, presentation, spec):
    checks = re.findall(r'\[threadcheck\] (\d+) pictures checked on (\d+) threads against one, (\d+) differ \(this one: (\d+) pixels\)', log)
    require(len(checks) >= spec["minimum_frames"], "too few worker/reference image comparisons")
    require([int(c[0]) for c in checks] == list(range(1, len(checks) + 1)), "worker comparisons are missing or repeated")
    require(all(int(threads) == spec["threads"] and int(mismatch) == 0 and int(pixels) == 0
                for count, threads, mismatch, pixels in checks), "parallel renderer differs from the single-worker reference")
    rows = [line.split() for line in presentation.splitlines() if line and line[0].isdigit()]
    require(all(len(r) == 6 for r in rows), "malformed presentation evidence")
    rows = [r for r in rows if int(r[1]) >= spec["clock"]]
    require(len(rows) >= spec["minimum_frames"], "missing moving presentation evidence")
    clocks = [int(r[1]) for r in rows]
    require(clocks == sorted(clocks), "presentation clocks go backwards")
    span = clocks[-1] - clocks[0]
    require(span >= spec["duration"] * 0.6, "worker comparisons do not span the moving scenario")
    plans = {tuple(r[2:5]) for r in rows if r[5] == '1' and int(r[3]) > int(r[2]) > 0 and math.isfinite(float(r[4]))}
    require(len(plans) >= spec["minimum_frames"], "worker comparison sequence is frozen or lacks interpolated pictures")
    return dict(frames_compared=len(checks), differing_frames=0, threads=spec["threads"], clock_span=span,
                distinct_plans=len(plans))


def check_shots(directory):
    files = sorted(directory.glob("*.bmp"))
    require(len(files) == 4, "expected four screenshots: two taps, another Tab, Ctrl+Alt+C; held repeat must be ignored")
    clocks = []
    for f in files:
        m = re.search(r"-clock(\d+)(?:-\d+)?\.bmp$", f.name)
        require(m, "screenshot filename lost replay clock")
        clocks.append(m.group(1))
        with Image.open(f) as im:
            require(im.format == "BMP" and im.width >= 320 and im.height >= 200, "invalid screenshot")
            require(im.convert("RGB").getbbox(), "screenshot is empty")
    require(len(set(clocks)) == 1, "paused screenshots did not retain the same clock")
    return {"captures": 4, "clock": clocks[0]}


def check_menu(log, path):
    states = re.findall(r"\[esc\] after key (\d+): settings (\d), keys (\d), strip (\d), paused (\d)", log)
    got = {int(k): tuple(map(int, rest)) for k, *rest in states}
    for k, state in {1: (1,0,1,1), 2: (0,1,1,1), 3: (1,0,1,1), 4: (0,0,0,0)}.items():
        require(got.get(k) == state, f"menu transition {k} failed")
    with Image.open(path) as image:
        im = image.convert("RGB")
        xs = [x for x in range(im.width) if im.getpixel((x, 6)) == (16,24,32)]
        require(xs, "menu strip was not drawn")
        left, right = min(xs), max(xs)
        require(not any(im.getpixel((x,y)) != (0,0,0) for y in range(6,30)
                        for x in range(im.width) if x < left or x > right), "menu text spills outside its strip")
    return {"transitions": 4, "strip_bounds": [left, right]}


class Runner:
    def __init__(self, args, manifest):
        self.args, self.manifest = args, manifest

    def run(self, name, session, extra, env=None, watch=False, config=False):
        root = self.args.out / name
        root.mkdir()
        source = self.args.sessions / session
        save = root / "save"
        shutil.copytree(source / "save-start", save)
        settings = root / "settings.ini"
        if config:
            settings.write_text("presentation = remastered\n")
        cmd = [str(self.args.app), *( ["--config", str(settings)] if config else ["--no-config"] ),
               "--data", str(self.args.data), "--save", str(save), "--no-record"]
        if watch:
            local_session = root / "sessions" / session
            local_session.parent.mkdir()
            shutil.copytree(source, local_session)
            cmd += ["--adlib-effects", "--watch", session]
        elif session and name not in ("input-live", "menu"):
            cmd += ["--adlib-effects"]
            if "--replay" not in extra:
                cmd += ["--replay", str(source / "input.log")]
        environment = clean_environment()
        for key, value in (env or {}).items():
            if value is None:
                environment.pop(key, None)
            else:
                environment[key] = value
        started = time.monotonic()
        with (root / "process.log").open("w") as log:
            proc = subprocess.run(cmd + extra, env=environment, stdout=log, stderr=log, timeout=self.args.timeout)
        require(proc.returncode == 0, f"{name}: app exited {proc.returncode}; see {root / 'process.log'}")
        return root, time.monotonic() - started

    def sound(self):
        spec = self.manifest["sound"]
        # Normal bay-door/brake keys, never driver calls or guest-memory edits.
        derived = self.args.out / "sound-input.log"
        derived_input(self.args.sessions / spec["session"] / "input.log", derived, spec)
        audit = self.args.out / "sound.jsonl"
        ram = self.args.out / "sound-observed-ram.bin"
        extra = ["--replay", str(derived), "--presentation", "original", "--seek", str(spec["exit_after"] / 60000000),
                 "--exit-after", str(spec["exit_after"])]
        root, elapsed = self.run("sound", spec["session"], extra,
            {"F117_HOST_ACCEPT_LOG": str(audit), "F117_DUMP_RAM": f"{spec['exit_after']}:{ram}"})
        result = check_sound(audit, spec)
        control = self.args.out / "sound-unobserved-ram.bin"
        _, control_elapsed = self.run("sound-control", spec["session"], extra,
            {"F117_DUMP_RAM": f"{spec['exit_after']}:{control}"})
        require(ram.stat().st_size == 1048576 and control.stat().st_size == 1048576 and digest(ram) == digest(control),
                "host audio diagnostics changed the guest-memory snapshot")
        result["observer_isolation_snapshot_sha256"] = digest(ram)
        return dict(name="sound", status="pass", seconds=elapsed, checks=result,
                    control_seconds=control_elapsed,
                    derived_input_sha256=digest(derived), log=str(root / "process.log"))

    def render(self, spec):
        prefix = self.args.out / f"{spec['name']}-frame-"
        root, elapsed = self.run(spec["name"], spec["session"],
            ["--presentation", "remastered", "--present-scale", "3", "--seek", str(spec["clock"] / 60000000),
             "--exit-after", str(spec["clock"] + spec["duration"])],
            {"F117_PRESENT_SHOTS": str(prefix), "F117_PRESENT_SHOTS_FROM": str(spec["clock"]),
             "F117_PRESENT_SHOTS_COUNT": "96", "F117_PRESENT_SHOTS_STEP": "2"})
        files = sorted(self.args.out.glob(prefix.name + "*.ppm"), key=lambda p: int(p.stem.rsplit("-",1)[-1]))
        pngs = []
        for p in files:
            dest = root / (p.stem + ".png")
            with Image.open(p) as im:
                im.save(dest)
            pngs.append(dest)
            p.unlink()  # only this run's generated, successfully converted PPM
        result = check_motion(pngs, spec)
        selected = [pngs[i] for i in sorted({0, len(pngs)//2, len(pngs)-1})]
        frames = [Image.open(p) for p in pngs]
        frames[0].save(root / "motion.gif", save_all=True, append_images=frames[1:], duration=100, loop=0)
        for im in frames:
            im.close()
        return dict(name=spec["name"], status="pass", seconds=elapsed, checks=result,
                    review_pending=spec["review"], images=[p.relative_to(self.args.out).as_posix() for p in selected],
                    animation=(root / "motion.gif").relative_to(self.args.out).as_posix())

    def workers(self):
        results, elapsed = [], 0.0
        for spec in self.manifest["workers"]:
            presentation = self.args.out / f"{spec['name']}-present.log"
            root, seconds = self.run(spec["name"], spec["session"],
                ["--presentation", "remastered", "--present-scale", "9", "--roland", "off",
                 "--seek", str(spec["clock"] / 60000000), "--exit-after", str(spec["clock"] + spec["duration"]),
                 "--present-log", str(presentation)],
                {"F117_PRESENT_THREADS_OFF": None, "F117_WINDOW": spec["window"],
                 "F117_REMASTER_THREADS": str(spec["threads"]), "F117_REMASTER_THREADCHECK": "3",
                 "F117_REMASTER_THREADCHECK_FROM": str(spec["clock"])})
            checks = check_workers((root / "process.log").read_text(), presentation.read_text(), spec)
            results.append(dict(name=spec["name"], checks=checks))
            elapsed += seconds
        return dict(name="renderer-workers", status="pass", seconds=elapsed, checks=results)

    def input(self, mode):
        session = self.manifest["sound"]["session"]
        root, elapsed = self.run("input-" + mode, session,
            ["--presentation", "original", "--exit-after", "1000000000"],
            {"F117_SHOT_AUTOTEST": "1"}, watch=mode == "watch")
        return dict(name="input-" + mode, status="pass", seconds=elapsed, checks=check_shots(root / "screenshots"))

    def menu(self):
        shot = self.args.out / "menu.ppm"
        root, elapsed = self.run("menu", self.manifest["sound"]["session"],
            ["--presentation", "remastered", "--present-scale", "3", "--exit-after", "1000000000"],
            {"F117_ESC_AUTOTEST": "1", "F117_MENU_SHOT": str(shot)}, config=True)
        return dict(name="menu", status="pass", seconds=elapsed,
                    checks=check_menu((root / "process.log").read_text(), shot))

    def settings(self):
        result=settings_acceptance.run(self.args.app,self.args.data,
            self.args.sessions/self.manifest["sound"]["session"],self.args.out/"settings")
        return dict(name="settings",status="pass",seconds=result["seconds"],checks=result["cases"],
            images=["settings/"+case["name"]+"/menu.png" for case in result["cases"]
                    if (self.args.out/"settings"/case["name"]/"menu.png").exists()])


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--app", type=Path, required=True)
    ap.add_argument("--data", type=Path)
    ap.add_argument("--sessions", type=Path)
    ap.add_argument("--out", type=Path, help="new or empty evidence directory")
    ap.add_argument("--verify-report", type=Path, help="verify complete evidence against this executable and current checker/manifest")
    ap.add_argument("--manifest", type=Path, default=ROOT / "docs/host-acceptance.json")
    ap.add_argument("--only", choices=("all", "sound", "render", "input"), default="all")
    ap.add_argument("--jobs", type=int, choices=(1,2), default=2)
    ap.add_argument("--timeout", type=int, default=180)
    args = ap.parse_args()
    if args.verify_report:
        try:
            verify_report(args.verify_report, args.app, args.manifest)
        except (OSError, ValueError, KeyError) as exc:
            print(str(exc), file=sys.stderr)
            return 1
        print("Host integration evidence matches. Machine parity and owner/GOG acceptance remain separate.")
        return 0
    if not (args.data and args.sessions and args.out):
        ap.error("--data, --sessions and --out are required unless --verify-report is used")
    for key in ("app", "data", "sessions", "out", "manifest"):
        setattr(args, key, getattr(args, key).resolve())
    report = dict(scope=args.only, full_suite=args.only == "all", machine_parity="not_run",
                  owner_reference_comparison="pending", cases=[], checker_sha256=digest(__file__),
                  settings_checker_sha256=digest(settings_acceptance.__file__),
                  started_utc=datetime.now(timezone.utc).isoformat())
    try:
        require(not args.out.exists() or not any(args.out.iterdir()), "output directory must be new or empty; stale evidence is not accepted")
        args.out.mkdir(parents=True, exist_ok=True)
        require(args.app.is_file() and args.data.is_dir(), "missing app executable or game data directory")
        manifest = json.loads(args.manifest.read_text())
        require(manifest["version"] == 1, "unsupported manifest version")
        report.update(app_sha256=digest(args.app), manifest_sha256=digest(args.manifest))
        needed = set(manifest["sessions"]) if args.only in ("all", "render") else {manifest["sound"]["session"]}
        for session in needed:
            root = args.sessions / session
            require((root / "save-start").is_dir(), f"missing save-start: {session}")
            require(digest(root / "input.log") == manifest["sessions"][session], f"recording hash mismatch: {session}")
        report["recordings"] = {s: manifest["sessions"][s] for s in sorted(needed)}
        runner = Runner(args, manifest)
        tasks = []
        if args.only in ("all", "sound"):
            tasks.append(("sound", runner.sound, ()))
        if args.only in ("all", "render"):
            tasks.extend((s["name"], runner.render, (s,)) for s in manifest["render"])
            tasks.append(("renderer-workers", runner.workers, ()))
        if args.only in ("all", "input"):
            tasks.extend(("input-"+mode, runner.input, (mode,)) for mode in ("live", "replay", "watch"))
            tasks.append(("menu", runner.menu, ()))
            tasks.append(("settings",runner.settings,()))
        with concurrent.futures.ThreadPoolExecutor(args.jobs) as pool:
            futures = [(name, pool.submit(fn, *values)) for name, fn, values in tasks]
            for name, future in futures:
                try:
                    result = future.result()
                except Exception as exc:
                    result = dict(name=name, status="fail", error=str(exc))
                report["cases"].append(result)
                print(f"{name}: {result['status']} {result.get('error', '')}", flush=True)
        report["status"] = "pass" if report["cases"] and all(c["status"] == "pass" for c in report["cases"]) else "fail"
    except Exception as exc:
        print(str(exc), file=sys.stderr)
        report.update(status="fail", error=str(exc))
    if args.out.exists() and not (report.get("error", "").startswith("output directory")):
        (args.out / "report.json").write_text(json.dumps(report, indent=2) + "\n")
        gallery = ["<!doctype html><meta charset='utf-8'><title>Host acceptance</title>",
                   "<style>body{font:17px system-ui;background:#151c26;color:#eee;max-width:1280px;margin:30px auto}img{max-width:100%;image-rendering:pixelated}</style>",
                   f"<h1>Host integration: {report['status']}</h1><p>Machine parity: not run. Owner/GOG comparison: pending. Scope: {args.only}.</p>"]
        for c in report["cases"]:
            gallery.append(f"<h2>{c['name']}: {c['status']}</h2>")
            for item in c.get("review_pending", []):
                gallery.append(f"<p>Review pending: {item}</p>")
            if "animation" in c:
                gallery.append(f"<img src='{c['animation']}'>")
            for path in c.get("images",[]):gallery.append(f"<img src='{path}'>")
        (args.out / "index.html").write_text("\n".join(gallery))
    return 0 if report["status"] == "pass" else 1


if __name__ == "__main__":
    raise SystemExit(main())
