"""Reject mixed CPU profiles and timing matches based on duration alone."""
import csv
import json
import sys
import tempfile
import unittest
from pathlib import Path

from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools" / "ref86box"))
import compare_timing86 as timing


def scene(digest, time=0, duration=2):
    return dict(hash=digest, time=time, duration=duration)


class TimingVerdicts(unittest.TestCase):
    def test_content_and_order_required(self):
        self.assertEqual(timing.pair([scene("a")], [scene("b")]), [])
        pairs = timing.pair([scene("b"), scene("a")], [scene("a"), scene("b")])
        self.assertEqual(len(pairs), 1)
        self.assertEqual(pairs[0][0]["hash"], "b")

    def test_bad_duration_is_measured_not_filtered(self):
        ours = [scene(str(i), i * 3) for i in range(17)]
        box = [dict(s, time=s["time"] + 50) for s in ours]
        self.assertTrue(timing.judge(ours, box)["pass_"])
        box[4]["duration"] += .4
        result = timing.judge(ours, box)
        self.assertEqual(result["paired"], 17)
        self.assertFalse(result["pass_"])

    def test_missing_scene_and_empty_capture_fail(self):
        ours = [scene(str(i), i * 3) for i in range(18)]
        self.assertFalse(timing.judge(ours, ours[:17])["pass_"])
        self.assertFalse(timing.judge([], [])["pass_"])

    def test_drift_limit(self):
        ours = [scene(str(i), i * 3) for i in range(17)]
        box = [dict(s) for s in ours]
        box[-1]["time"] += 2.3
        self.assertFalse(timing.judge(ours, box)["pass_"])

    def test_reference_screen_off_and_censored_end(self):
        with tempfile.TemporaryDirectory() as root:
            path = Path(root) / "frames.csv"
            with path.open("w", newline="", encoding="utf-8") as out:
                writer = csv.writer(out)
                for i in range(80):
                    writer.writerow([i, 0, round(i * timing.FRAME_SECONDS * 1e6), 640, 400, "a"])
                writer.writerow([80, 0, 4_000_000, 640, 400, "b"])
                writer.writerow([81, 0, 6_000_000, 640, 400, "c"])
            result = timing.box_scenes(path)
            self.assertEqual([s["hash"] for s in result], ["a"])
            self.assertAlmostEqual(result[0]["duration"], 80 * timing.FRAME_SECONDS, places=6)


class CaptureIdentity(unittest.TestCase):
    def test_profile_required(self):
        with tempfile.TemporaryDirectory() as root:
            run = Path(root)
            for command in ([], ["--ips", "9000000"], ["--timing", "286"],
                            ["--timing", "386", "--ips", "9000000"]):
                (run / "settings.json").write_text(json.dumps(dict(command=command)), encoding="utf-8")
                with self.assertRaises(ValueError):
                    timing.ours_scenes(run)
            with self.assertRaises(ValueError):
                timing.ours_scenes(run / "comparison.json")

    def test_reference_pixel_hash(self):
        with tempfile.TemporaryDirectory() as root:
            path = Path(root) / "solid.ppm"
            Image.new("RGB", (320, 200), (65, 130, 195)).save(path)
            # Independent FNV fixture: 128000 uint64 words 0xff4081c2ff4081c2.
            self.assertEqual(timing.picture_hash(path), "5f8c74b209738383")
            Image.new("RGB", (640, 400)).save(path)
            with self.assertRaises(ValueError):
                timing.picture_hash(path)

    def test_duplicate_pixels_keep_hold_and_setup_anchor(self):
        with tempfile.TemporaryDirectory() as root:
            run = Path(root)
            (run / "shots").mkdir()
            (run / "settings.json").write_text(json.dumps(dict(command=["--timing", "386"])), encoding="utf-8")
            (run / "input.log").write_text(f"K {timing.CLOCK} 03\n", encoding="utf-8")
            with (run / "frames.csv").open("w", newline="", encoding="utf-8") as out:
                writer = csv.DictWriter(out, fieldnames=["frame_icount", "icount", "written", "video_mode", "blank"])
                writer.writeheader()
                for second in (0, 1, 2, 4):
                    count = second * timing.CLOCK
                    Image.new("RGB", (320, 200), (65, 130, 195) if second < 4 else (0, 0, 0)).save(
                        run / "shots" / f"shot_{count:011d}.ppm")
                    writer.writerow(dict(frame_icount=count, icount=count, written=1, video_mode=19, blank=0))
            result = timing.ours_scenes(run)
            self.assertEqual(len(result), 1)
            self.assertEqual(result[0]["time"], 1)
            self.assertEqual(result[0]["duration"], 3)
            self.assertEqual(result[0]["hash"], "5f8c74b209738383")


if __name__ == "__main__":
    unittest.main()
