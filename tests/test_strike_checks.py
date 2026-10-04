"""A shared final hash alone must not pass an uncompleted ground strike."""
import sys
from pathlib import Path
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from strike_pilot import errors

class StrikeChecks(unittest.TestCase):
    def setUp(self):
        self.before = dict(objective_type=2, target_damaged=0, flags=0x1005,
            hit_events=0, launch_events=0, ejection=0, fuel=5000, agl=2500,
            ground=0, stations=[dict(stores=2)])
        self.after = dict(self.before, target_damaged=1, flags=0x5005,
            hit_events=1, launch_events=2, stations=[dict(stores=0)])
    def test_primary_credit_is_not_a_crash_flag(self):
        self.assertEqual(errors([self.before, self.after]), [])
    def test_require_damage_credit_hit_launch_and_store(self):
        for change in (dict(target_damaged=0), dict(flags=0x1005), dict(hit_events=0),
                       dict(launch_events=0), dict(stations=[dict(stores=2)])):
            self.assertTrue(errors([self.before, dict(self.after, **change)]))
    def test_reject_wrong_mission_and_loss(self):
        for change in (dict(objective_type=1), dict(ejection=1), dict(agl=0), dict(fuel=0)):
            self.assertTrue(errors([self.before, dict(self.after, **change)]))
if __name__ == "__main__": unittest.main()
