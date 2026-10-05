"""Equal hashes cannot excuse an unearned or off-strip landing."""
import sys
from pathlib import Path
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from airstrip_check import errors


class AirstripChecks(unittest.TestCase):
    def setUp(self):
        self.before = dict(objective_type=4, flags=1, strip_events=0, store_count=1,
            box=0, nearest=58, target=24, x=10496, y=3900, target_x=10496,
            target_y=3840, box_width=288, box_length=1280, agl=2500, ground=0,
            speed=500, throttle=100, ejection=0, fuel=8000, target_damaged=0)
        self.after = dict(self.before, flags=0x4008, strip_events=1, store_count=0,
            box=1, nearest=24, y=3809, agl=0, speed=0, throttle=0, fuel=7261)

    def test_original_credit_and_stop(self):
        self.assertEqual(errors([self.before, self.after]), [])

    def test_require_credit_event_consumption_and_type(self):
        for change in (dict(flags=8), dict(strip_events=0), dict(store_count=1),
                       dict(objective_type=3)):
            self.assertTrue(errors([self.before, dict(self.after, **change)]))

    def test_reject_off_strip_stop_and_loss(self):
        for change in (dict(y=3775), dict(nearest=58), dict(agl=1), dict(speed=5),
                       dict(throttle=1), dict(flags=0x4000), dict(fuel=0),
                       dict(ejection=1), dict(target_damaged=1)):
            self.assertTrue(errors([self.before, dict(self.after, **change)]))

    def test_ground_taxi_cannot_pass(self):
        self.assertTrue(errors([dict(self.before, agl=0), self.after]))

    def test_return_preserves_delivery_despite_later_target_damage(self):
        depart = dict(self.after, agl=2000, speed=400, throttle=100, box=0)
        home = dict(self.after, nearest=58, target_damaged=1)
        self.assertEqual(errors([self.before, self.after, depart, home], complete=True), [])
        damaged_delivery = dict(self.after, target_damaged=1)
        self.assertTrue(errors([self.before, damaged_delivery, depart, home], complete=True))

    def test_return_requires_a_second_airborne_leg_and_retained_credit(self):
        self.assertTrue(errors([self.before, self.after, self.after], complete=True))
        depart = dict(self.after, agl=2000, speed=400, throttle=100, box=0)
        for change in (dict(flags=8), dict(strip_events=0), dict(store_count=1),
                       dict(objective_type=3), dict(ejection=1), dict(fuel=0)):
            self.assertTrue(errors([self.before, self.after, depart,
                                    dict(self.after, **change)], complete=True))


if __name__ == "__main__":
    unittest.main()
