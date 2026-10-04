"""Photo credit requires the original exposure, event and intact target."""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from recon_pilot import recon_errors


class ReconChecks(unittest.TestCase):
    def setUp(self):
        self.state = dict(agl=2129, ground=0, objective_type=1, flags=16901,
            photos=1, credit_events=1, target_damaged=0, ejection=0,
            weapon=16, store_count=1)

    def errors(self, **changes):
        return recon_errors([dict(self.state, **changes)])

    def test_original_credit(self):
        self.assertEqual(self.errors(), [])

    def test_missing_exposure_or_event(self):
        self.assertTrue(self.errors(photos=0))
        self.assertTrue(self.errors(credit_events=0))
        self.assertTrue(self.errors(flags=517))
        self.assertTrue(self.errors(photos=2))

    def test_crash_and_destroyed_target(self):
        self.assertTrue(self.errors(target_damaged=1))
        self.assertTrue(self.errors(ejection=1))
        self.assertTrue(self.errors(agl=0))

    def test_wrong_objective_or_empty_camera(self):
        self.assertTrue(self.errors(objective_type=2))
        self.assertTrue(self.errors(weapon=5))
        self.assertTrue(self.errors(store_count=0))

    def test_both_photo_events_required(self):
        row = dict(self.state, flags=0x6204, photos=2, secondary_type=1,
                   secondary_credit_events=1, secondary_damaged=0)
        self.assertEqual(recon_errors([row], complete=True), [])
        for changes in (dict(secondary_credit_events=0), dict(secondary_damaged=1),
                        dict(flags=0x4204), dict(secondary_type=2), dict(photos=1)):
            self.assertTrue(recon_errors([dict(row, **changes)], complete=True))


if __name__ == "__main__":
    unittest.main()
