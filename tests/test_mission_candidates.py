"""Candidate searches vary normal input without scrambling frontend order."""
import sys
from pathlib import Path
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from mission_candidates import delayed_front


class MissionCandidates(unittest.TestCase):
    def test_delay_moves_briefing_and_all_later_start_inputs(self):
        original = ["--click", "START.EXE+400:28,84", "--click", "START.EXE+1100:280,60",
                    "--click", "START.EXE+1700:272,134", "--type", "SETUP.EXE+200:N"]
        shifted = delayed_front(original, 90, 1100)
        self.assertEqual(shifted, ["--click", "START.EXE+400:28,84",
            "--click", "START.EXE+1190:280,60", "--click", "START.EXE+1790:272,134",
            "--type", "SETUP.EXE+200:N"])
        self.assertEqual(original[3], "START.EXE+1100:280,60")


if __name__ == "__main__":
    unittest.main()
