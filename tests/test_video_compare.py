"""ROM-free checks: pixel changes and reordered pictures cannot pass alignment."""
import contextlib
import io
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from video_compare import append_frame, compare


def frames(values):
    out = []
    for i, value in enumerate(values):
        append_frame(out, bytes([value]) * 12, i / 70, str(i), 1 / 70)
    return out


class VideoCompareTest(unittest.TestCase):
    def compare(self, a, b):
        with contextlib.redirect_stdout(io.StringIO()):
            return compare(frames(a), frames(b))

    def test_identical_with_different_capture_start_and_repeats(self):
        r = self.compare([9, 1, 1, 2, 3, 3], [1, 1, 2, 3, 3, 8])
        self.assertEqual(r["exact_matches"], 3)
        self.assertEqual(r["reference_unmatched"], [0])
        self.assertEqual(r["our_unmatched"], [])
        self.assertEqual(r["our_excluded"], [0, 1])

    def test_unmatched_capture_end_is_not_hidden(self):
        r = self.compare([1, 2, 3, 4], [1, 2, 3, 5])
        self.assertEqual(r["reference_unmatched"], [3])
        self.assertEqual(r["our_unmatched"], [3])

    def test_changed_pixel_is_reported(self):
        a, b = frames([1, 2, 3]), frames([1, 2, 3])
        append_frame(b, b"\x03" * 11 + b"\x04", 3 / 70, "changed", 1 / 70)
        # Put the changed image between exact anchors.
        b[1] = b.pop()
        b[1]["time"], b[1]["end"] = 1 / 70, 2 / 70
        with contextlib.redirect_stdout(io.StringIO()):
            r = compare(a, b)
        self.assertEqual(r["reference_unmatched"], [1])
        self.assertEqual(r["our_unmatched"], [1])

    def test_reordered_pictures_are_reported(self):
        r = self.compare([1, 2, 3, 4], [1, 3, 2, 4])
        self.assertTrue(r["reference_unmatched"])
        self.assertTrue(r["our_unmatched"])

    def test_no_common_picture_fails(self):
        with self.assertRaisesRegex(RuntimeError, "no exact RGB"):
            self.compare([1, 2], [3, 4])


if __name__ == "__main__":
    unittest.main()
