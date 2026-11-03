"""Verdict regressions: known broken paths must not produce a green report.

These ROM-free tests exercise the acceptance checker, not the real host. The
recorded-flight command remains a separate required integration check.
"""
import copy
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import host_acceptance as host


def sound_fixture():
    spec = dict(coast=[1000, 2000], overlap=[300, 500], stop=[3000, 4000],
                minimum_rms=8, maximum_stopped_rms=8, minimum_coast_updates=10, effect_id=6,
                engine_pitch_register=166, minimum_pitch_values=2, effect_keyon_register=178)
    rows = [dict(kind="format", version=1, sample_rate=44100, ips=1000)]
    def request(clock, ident, value=0, thrust=0):
        return dict(kind="request", clock=clock, id=ident, value=value,
                    observed_knots=value, thrust=thrust, ok=1)
    rows += [request(100, 24), request(100, 23), request(200, 23, 10, 100),
             request(300, 6), request(300, 23, 15, 100)]
    rows += [request(c, 23, 20) for c in range(1000, 2001, 100)]
    rows += [request(c, 23) for c in range(3000, 4001, 50)]
    rows += [dict(kind="opl",clock=1000,reg=166,value=151),dict(kind="opl",clock=1500,reg=166,value=145)]
    rows += [dict(kind="opl",clock=300,reg=178,value=35)]
    for clock in range(0, 4000, 100):
        first = clock * 44100 // 1000
        rms = 32 if 100 <= clock < 2000 else 0
        rows.append(dict(kind="audio", first=first, frames=4410, side_first=first,
                         side_frames=4410, side_energy=rms*rms*8820, mix_energy=rms*rms*8820))
    return spec, rows


class SoundVerdicts(unittest.TestCase):
    def check(self, rows, spec):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "audio.jsonl"
            path.write_text("".join(json.dumps(a) + "\n" for a in rows))
            return host.check_sound(path, spec)

    def test_complete_route_and_pcm(self):
        spec, rows = sound_fixture()
        result = self.check(rows, spec)
        self.assertEqual(result["coast_updates"], 11)
        self.assertEqual(result["side_energy"]["rms"], 32)

    def test_known_sound_failures_are_rejected(self):
        for failure in ("missing-route", "lost-argument", "silent-sidecar", "silent-mixer",
                        "audio-gap", "wrong-clock", "missing-effect", "failed-request",
                        "still-moving", "stuck-voice", "missing-format", "duplicate-speed", "fixed-pitch", "unplayed-effect"):
            with self.subTest(failure=failure):
                spec, rows = sound_fixture()
                if failure == "missing-route":
                    rows = [a for a in rows if a.get("id") != 23]
                elif failure == "lost-argument":
                    for a in rows:
                        if a.get("id") == 23:
                            a["value"] = 0
                elif failure in ("silent-sidecar", "silent-mixer"):
                    for a in rows:
                        if a["kind"] == "audio":
                            a["side_energy" if failure == "silent-sidecar" else "mix_energy"] = 0
                elif failure == "audio-gap":
                    rows = [a for a in rows if a.get("first") != 44100]
                elif failure == "wrong-clock":
                    for a in rows:
                        if a["kind"] == "audio":
                            a["side_first"] += 1
                elif failure == "missing-effect":
                    rows = [a for a in rows if a.get("id") != 6]
                elif failure == "failed-request":
                    rows[1]["ok"] = 0
                elif failure == "still-moving":
                    for a in rows:
                        if a.get("id") == 23 and a["clock"] >= 3000:
                            a["value"] = 1
                elif failure == "stuck-voice":
                    for a in rows:
                        if a["kind"] == "audio" and a["first"] >= 132300:
                            a["side_energy"] = 32*32*8820
                elif failure == "missing-format":
                    rows.pop(0)
                elif failure == "duplicate-speed":
                    rows.insert(6, copy.deepcopy(rows[6]))
                elif failure == "fixed-pitch":
                    for a in rows:
                        if a["kind"] == "opl":
                            a["value"] = 151
                elif failure == "unplayed-effect":
                    rows = [a for a in rows if a.get("reg") != 178]
                with self.assertRaises((ValueError, KeyError)):
                    self.check(rows, spec)

    def test_request_log_alone_does_not_prove_audibility(self):
        spec, rows = sound_fixture()
        rows = [a for a in rows if a["kind"] != "audio"]
        with self.assertRaisesRegex(ValueError, "cover"):
            self.check(rows, spec)


class ImageVerdicts(unittest.TestCase):
    def test_light_stripe(self):
        im = Image.new("RGB", (12,12))
        for y in range(4,7):
            for x in range(4,7):
                im.putpixel((x,y), (255,255,170))
        feature = dict(kind="cream_component", roi=[0,0,12,12], minimum_area=8)
        self.assertTrue(host.feature_ok(im, feature))
        for x in range(4,7):
            im.putpixel((x,5), (20,20,20))
        self.assertFalse(host.feature_ok(im, feature))

    def test_flag_endpoint_and_occlusion_features(self):
        im = Image.new("RGB", (12,12))
        feature = dict(kind="flag_component", roi=[0,0,12,12], minimum_area=8)
        for y in range(4,7):
            im.putpixel((4,y), (52,16,16))
        self.assertFalse(host.feature_ok(im, feature))
        for y in range(4,7):
            for x in range(4,8):
                im.putpixel((x,y), (52,16,16))
        self.assertTrue(host.feature_ok(im, feature))
        im = Image.new("RGB", (12,12), (20,20,20))
        dark = dict(kind="dark_fraction", roi=[0,0,12,12], minimum=0.95)
        self.assertTrue(host.feature_ok(im, dark))
        im.paste((60,60,60), (0,0,12,12))
        self.assertFalse(host.feature_ok(im, dark))

    def test_hidden_yellow_and_water_cutoff(self):
        im = Image.new("RGB", (12,12), (12,70,102))
        sea = dict(kind="water_fraction", roi=[0,0,12,12], minimum=0.98)
        yellow = dict(kind="no_yellow", roi=[0,0,12,12])
        self.assertTrue(host.feature_ok(im, sea))
        im.paste((70,66,52), (0,10,12,12))
        self.assertFalse(host.feature_ok(im, sea))
        self.assertTrue(host.feature_ok(im, yellow))
        im.putpixel((5,5), (178,154,24))
        self.assertFalse(host.feature_ok(im, yellow))

    def test_old_shoreline_and_missing_ground_are_rejected(self):
        feature = dict(kind="ground_fraction", roi=[0,0,12,12], minimum=0.98)
        im = Image.new("RGB", (12,12), (70,66,52))
        self.assertTrue(host.feature_ok(im, feature))
        for colour in ((12,70,102), (0,0,0), (40,40,40), (255,255,170), (20,255,20)):
            im.paste(colour, (0,0,12,12))
            self.assertFalse(host.feature_ok(im, feature))
        im.paste((70,66,52), (0,0,12,12))
        im.paste((12,70,102), (0,10,12,12))
        self.assertFalse(host.feature_ok(im, feature))

    def test_frozen_sequence_is_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            files = []
            for i in range(12):
                p = Path(tmp) / f"frame-{100+i*10}.png"
                Image.new("RGB", (1280,600), (12,70,102)).save(p)
                files.append(p)
            with self.assertRaisesRegex(ValueError, "frozen"):
                host.check_motion(files, dict(clock=100, duration=100, features=[]))


class InputVerdicts(unittest.TestCase):
    def test_paused_unique_screenshots_and_held_repeat(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            for suffix in ("", "-1", "-2", "-3"):
                Image.new("RGB", (640,400), (20,20,20)).save(root / f"session-clock123{suffix}.bmp")
            self.assertEqual(host.check_shots(root)["captures"], 4)
            Image.new("RGB", (640,400)).save(root / "session-clock123-4.bmp")
            with self.assertRaisesRegex(ValueError, "four screenshots"):
                host.check_shots(root)

    def test_menu_spill_and_state_transitions(self):
        log = "\n".join(f"[esc] after key {k}: settings {s}, keys {r}, strip {t}, paused {p}"
                        for k,s,r,t,p in [(1,1,0,1,1),(2,0,1,1,1),(3,1,0,1,1),(4,0,0,0,0)])
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "menu.ppm"
            im = Image.new("RGB", (640,400))
            im.paste((16,24,32), (40,6,600,30))
            im.putpixel((530,14), (112,112,112))
            im.save(path)
            self.assertEqual(host.check_menu(log,path)["transitions"], 4)
            im.paste((0,0,0), (40,6,120,30))
            im.paste((0,0,0), (520,6,600,30))
            im.putpixel((530,14), (112,112,112))
            im.save(path)
            with self.assertRaisesRegex(ValueError, "spills"):
                host.check_menu(log,path)
            with self.assertRaisesRegex(ValueError, "transition"):
                host.check_menu(log.replace("paused 1", "paused 0"),path)

    def test_derived_inputs_preserve_recording_and_equal_clock_order(self):
        with tempfile.TemporaryDirectory() as tmp:
            source, dest = Path(tmp)/"input.log", Path(tmp)/"derived.log"
            original = "# f117r-input ips=60000000\nK 10 2A\nK 10 0D\nK 20 AA\nK 40 80\n"
            source.write_text(original)
            host.derived_input(source, dest, dict(keep_until=20, inject_keys=[[15,"0E"]]))
            self.assertEqual(source.read_text(), original)
            self.assertEqual(dest.read_text(), "# f117r-input ips=60000000\nK 10 2A\nK 10 0D\nK 15 0E\nK 20 AA\n")


class ReportVerdicts(unittest.TestCase):
    def test_worker_comparisons_reject_wrong_threads_changed_pixels_and_frozen_motion(self):
        spec = dict(threads=8, minimum_frames=8, clock=100, duration=200)
        lines = [f'[threadcheck] {i} pictures checked on 8 threads against one, 0 differ (this one: 0 pixels)'
                 for i in range(1, 9)]
        presentation = '\n'.join(f'{i*1000} {100+i*30} {i+1} {i+2} 0.25 1' for i in range(8))
        log = '\n'.join(lines)
        self.assertEqual(host.check_workers(log, presentation, spec)['frames_compared'], 8)
        for bad_log, bad_present in [('',presentation), ('\n'.join(lines[:-1]),presentation),
                (log.replace('on 8 threads','on 4 threads'),presentation),
                (log.replace('0 differ','1 differ'),presentation),
                (log.replace('0 pixels','1 pixels'),presentation),
                (log+'\n'+lines[-1],presentation),
                (log,'\n'.join(f'{i*1000} {100+i*30} 1 2 0.25 1' for i in range(8))),
                (log,'\n'.join(f'{i*1000} {100+i} {i+1} {i+2} 0.25 1' for i in range(8))),
                (log,presentation.replace(' 0.25 1',' 0.25 0'))]:
            with self.subTest(log=bad_log,presentation=bad_present), self.assertRaises(ValueError):
                host.check_workers(bad_log,bad_present,spec)

    def test_partial_stale_and_incomplete_reports_are_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            app, manifest, path = root/"app", root/"manifest.json", root/"report.json"
            app.write_text("built app")
            m = dict(render=[dict(name="moving")], sessions={"s":"recorded hash"})
            manifest.write_text(json.dumps(m))
            good = dict(status="pass",full_suite=True,scope="all",app_sha256=host.digest(app),
                        manifest_sha256=host.digest(manifest),checker_sha256=host.digest(host.__file__),
                        recordings=m["sessions"],machine_parity="not_run",owner_reference_comparison="pending",
                        cases=[dict(name=n,status="pass") for n in ["sound","input-live","input-replay","input-watch","menu","moving","renderer-workers"]])
            path.write_text(json.dumps(good))
            host.verify_report(path,app,manifest)
            for key,value in [("full_suite",False),("app_sha256","old"),("checker_sha256","old"),
                              ("recordings",{}),("machine_parity","pass"),("status","skip"),
                              ("cases",good["cases"][:-1])]:
                bad = copy.deepcopy(good)
                bad[key] = value
                path.write_text(json.dumps(bad))
                with self.subTest(key=key), self.assertRaises(ValueError):
                    host.verify_report(path,app,manifest)

    def test_missing_recordings_fail_without_claiming_parity(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            app = root/"app.exe"
            app.write_text("not executed")
            args = ["host_acceptance.py", "--app",str(app), "--data",str(root),
                    "--sessions",str(root/"missing"), "--out",str(root/"out")]
            with patch.object(sys,"argv",args):
                self.assertEqual(host.main(),1)
            report = json.loads((root/"out/report.json").read_text())
            self.assertEqual(report["status"],"fail")
            self.assertEqual(report["machine_parity"],"not_run")
            self.assertEqual(report["owner_reference_comparison"],"pending")


if __name__ == "__main__":
    unittest.main()
