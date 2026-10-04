"""Keep the landing acceptance gate from accepting crashes or early stops."""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from landing_pilot import landing_errors


class LandingChecks(unittest.TestCase):
    def setUp(self):
        self.last = dict(agl=0, ground=0, x=9793, y=1539, home_x=9792,
            home_y=1600, box=1, nearest=33, home=33, box_width=288,
            box_length=2304, speed=0, throttle=0, flags=4104, ejection=0,
            fuel=4651, stopped=2, S=11)
        self.airborne = dict(self.last, agl=2000, speed=400, stopped=1)
        self.report = dict(mission_result=0, pilot_status=3)
        self.log = "[exit] VGAME.EXE terminated with code 129 -> back to F117.COM @9786379482\n"

    def errors(self, **changes):
        return landing_errors([self.airborne, dict(self.last, **changes)], self.report, self.log)

    def test_success_and_countdown(self):
        self.assertEqual(self.errors(), [])
        self.assertTrue(self.errors(stopped=1))
        self.assertTrue(self.errors(speed=2))
        self.assertTrue(self.errors(throttle=10))

    def test_geometric_box_and_home(self):
        self.assertTrue(self.errors(x=9802))
        self.assertTrue(self.errors(y=1527))
        self.assertTrue(self.errors(nearest=34))
        self.assertTrue(self.errors(agl=1))

    def test_crash_ejection_and_equipment(self):
        self.assertTrue(self.errors(ejection=1))
        self.assertTrue(self.errors(flags=4105))
        self.assertTrue(self.errors(flags=4096))
        self.assertTrue(self.errors(fuel=0))
        self.report["mission_result"] = 1
        self.assertTrue(self.errors())

    def test_early_exit_and_ground_taxi(self):
        self.assertTrue(landing_errors([self.last], self.report, self.log))
        self.assertTrue(landing_errors([self.airborne, self.last], self.report, ""))
        self.report["pilot_status"] = 0
        self.assertTrue(self.errors())


if __name__ == "__main__":
    unittest.main()
