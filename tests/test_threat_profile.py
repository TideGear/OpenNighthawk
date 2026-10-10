"""The exposure pilot must steer inward outside its orbit in every quadrant."""
import math
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from threat_profile import orbit_heading, signed
from threat_compare import paired, interval


class ThreatOrbitChecks(unittest.TestCase):
    def test_word_differences_wrap_in_both_directions(self):
        for value, expected in ((-1, -1), (-65537, -1), (65535, -1),
                                (65537, 1), (32768, -32768), (-32769, 32767)):
            self.assertEqual(signed(value), expected)

    def test_radial_velocity_corrects_radius_in_every_quadrant(self):
        for angle in range(0, 360, 45):
            theta = math.radians(angle)
            for radius in (5000, 6000, 7000):
                dx, dy = round(radius * math.sin(theta)), round(-radius * math.cos(theta))
                heading = orbit_heading(16000 + dx, 16000 + dy, 16000, 16000, 6000)
                radians = heading * math.pi / 32768
                radial = dx * math.sin(radians) - dy * math.cos(radians)
                with self.subTest(angle=angle, radius=radius):
                    if radius < 6000:
                        self.assertGreater(radial, 0)
                    elif radius > 6000:
                        self.assertLess(radial, 0)
                    else:
                        self.assertLess(abs(radial), 3)


class ThreatPairChecks(unittest.TestCase):
    def test_clock_shift_alone_does_not_make_a_matching_mission(self):
        baseline = dict(route="strike", speed="9", offset_ms=2500,
                        mission={"target": 1}, fixes=[])
        fast = dict(baseline, speed="20", offset_ms=2720, mission={"target": 2})
        self.assertEqual(paired([baseline, fast], "9", "20", {"20": 220}), [])
        fast["mission"] = baseline["mission"]
        self.assertEqual(paired([baseline, fast], "9", "20", {}), [])
        self.assertEqual(paired([baseline, fast], "9", "20", {"20": 220}), [(baseline, fast)])

    def test_early_exit_remains_a_pair_but_errors_do_not(self):
        baseline = dict(route="strike", speed="20", offset_ms=0,
                        mission={"target": 1}, fixes=[])
        fixed = dict(baseline, fixes=["D1TTL"], exited="left VGAME")
        self.assertEqual(len(paired([baseline, fixed], "20", "20+D1TTL", {})), 1)
        fixed["error"] = "failed flight"
        self.assertEqual(paired([baseline, fixed], "20", "20+D1TTL", {}), [])

    def test_bootstrap_uses_paired_differences(self):
        self.assertEqual(interval([2, 2, 2]), (2, 2))


if __name__ == "__main__":
    unittest.main()
