"""Supply-drop parity requires actual arrival despite original no-credit."""
import sys
from pathlib import Path
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from cargo_check import errors


class CargoChecks(unittest.TestCase):
    def setUp(self):
        self.before = dict(clock=0, objective_type=3, flags=1, strip_events=0, weapon=18,
            store_count=1, launch_events=0, agl=2500, ground=0, ejection=0,
            fuel=8000, target_damaged=0, cargo_slot=-1, cargo_type=0,
            cargo_weapon=0, cargo_ttl=0, cargo_x=0, cargo_y=0, cargo_z=0,
            impact_x=0, impact_y=0, impact_z=0, target_x=10496,
            target_y=3840, mission_time=588, deadline=1094)
        self.live = dict(self.before, clock=900000000, flags=4, store_count=0, launch_events=1,
            cargo_slot=11, cargo_type=38, cargo_weapon=18, cargo_ttl=985,
            cargo_x=10500, cargo_y=3955, cargo_z=15)
        self.impact = dict(self.live, clock=901800000, cargo_ttl=0, cargo_y=3951, cargo_z=-5,
            impact_x=10500, impact_y=3951, impact_z=-5, agl=217)

    def verdict(self, **change):
        impact = dict(self.impact, **change)
        return errors([self.before, self.live, impact, dict(impact, clock=912600000)])

    def test_original_impact_without_credit(self):
        self.assertEqual(self.verdict(), [])

    def test_expiry_or_consumption_is_not_impact(self):
        self.assertTrue(errors([self.before, dict(self.live, cargo_ttl=2), self.impact]))
        self.assertTrue(errors([self.before, self.live]))
        self.assertTrue(self.verdict(cargo_z=1, impact_z=1))
        self.assertTrue(errors([self.before, self.live, self.impact]))

    def test_original_area_and_deadline_boundaries(self):
        self.assertEqual(self.verdict(cargo_x=10496, impact_x=10496,
                                     cargo_y=4095, impact_y=4095), [])
        self.assertTrue(self.verdict(cargo_x=10496, impact_x=10496,
                                    cargo_y=4096, impact_y=4096))
        self.assertTrue(self.verdict(mission_time=1094))

    def test_track_actual_owned_cargo_impact(self):
        for change in (dict(cargo_slot=7), dict(cargo_type=30),
                       dict(cargo_weapon=9), dict(impact_x=10501),
                       dict(launch_events=0), dict(store_count=1),
                       dict(objective_type=4)):
            self.assertTrue(self.verdict(**change))

    def test_credit_or_aircraft_loss_cannot_pass(self):
        for change in (dict(flags=0x4004), dict(strip_events=1), dict(fuel=0),
                       dict(target_damaged=1), dict(ejection=1), dict(agl=0)):
            self.assertTrue(self.verdict(**change))


if __name__ == "__main__":
    unittest.main()
