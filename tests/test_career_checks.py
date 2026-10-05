"""ROM-free verdict checks; these do not simulate or earn a career."""
import struct
import sys
from pathlib import Path
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from career_check import career, career_errors


class CareerChecks(unittest.TestCase):
    def setUp(self):
        self.before = dict(pilot=9, rank=1, score=217, total=500, sorties=3, status=0)
        self.after = dict(self.before, total=717, sorties=4)

    def test_saved_progress(self):
        self.assertEqual(career_errors(self.before, self.after), [])

    def test_count_score_total_and_pilot_must_all_agree(self):
        for change in (dict(sorties=3), dict(sorties=5), dict(score=0),
                       dict(score=65535), dict(total=716), dict(pilot=8),
                       dict(rank=0), dict(status=2)):
            self.assertTrue(career_errors(self.before, dict(self.after, **change)))

    def test_unavailable_starting_pilot_is_rejected(self):
        for status in (1, 2):
            self.assertTrue(career_errors(dict(self.before, status=status), self.after))

    def test_retirement_verdict_requires_actual_status_change(self):
        before = dict(self.before, sorties=98)
        after = dict(self.after, sorties=99, status=1)
        self.assertEqual(career_errors(before, after), [])
        self.assertTrue(career_errors(before, dict(after, status=0)))
        self.assertTrue(career_errors(before, dict(after, status=2)))

    def test_status_is_at_record_4e_not_the_theatre_word(self):
        data = bytearray(802)
        struct.pack_into("<H", data, 2 + 0x38, 8)
        struct.pack_into("<H", data, 2 + 0x4e, 1)
        self.assertEqual(career(data)["status"], 1)
        self.assertEqual(career(data)["sorties"], 0)

    def test_invalid_roster_length_and_selection(self):
        with self.assertRaises(ValueError): career(bytes(800))
        data = bytearray(802); struct.pack_into("<H", data, 0, 10)
        with self.assertRaises(ValueError): career(data)


if __name__ == "__main__":
    unittest.main()
