"""Regression for the post-submission timestamp marker found in a real capture."""
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

REPORT = Path(__file__).resolve().parents[1] / "tools/renderer_bench/timestamp_report.py"
FRAME = """from,to,gpu_ms
upload_begin,upload_end,0.194592
upload_end,bands_end,2.465760
bands_end,polygons_end,1.329152
polygons_end,points_end,0.041984
points_end,strokes_end,1.690624
strokes_end,picture_upload_begin,0.027648
picture_upload_begin,picture_upload_end,0.178560
picture_upload_end,replace_end,0.258688
replace_end,present_end,0.259072
present_end,download_end,0.461056
"""


class TimestampReportRegression(unittest.TestCase):
    def run_report(self, text):
        with tempfile.TemporaryDirectory() as directory:
            capture = Path(directory) / "capture.csv"
            capture.write_text(text, encoding="utf-8")
            return subprocess.run([sys.executable, str(REPORT), str(capture)],
                                  capture_output=True, text=True)

    def test_complete_hardware_capture(self):
        result = self.run_report(FRAME)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_post_submission_marker_is_rejected(self):
        # Actual failure: a marker was added after resolve/submit, then appeared
        # at the start of the following frame's query batch.
        bad = FRAME + "present_end,upload_begin,22.180864\n" + FRAME.split("\n", 1)[1]
        self.assertNotEqual(self.run_report(bad).returncode, 0)

    def test_interrupted_capture_is_rejected(self):
        self.assertNotEqual(self.run_report(FRAME.rsplit("present_end,", 1)[0]).returncode, 0)


if __name__ == "__main__":
    unittest.main()
