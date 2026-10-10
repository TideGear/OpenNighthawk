"""The exposure pilot must steer inward outside its orbit in every quadrant."""
import math
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from threat_profile import orbit_heading, signed, delay_launch, step_input, tag_of
from threat_compare import paired, interval, arm


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
    def test_mission_matching_does_not_imply_combat_seed_matching(self):
        slow = dict(route="strike", speed="9", offset_ms=0,
                    mission={"target": 1}, fixes=[], flight_seed=39037)
        fast = dict(slow, speed="20", flight_seed=39004)
        self.assertEqual(len(paired([slow, fast], "9", "20", {})), 1)
        self.assertEqual(paired([slow, fast], "9", "20", {}, True), [])
        for seed in (39037, 0):
            slow["flight_seed"] = fast["flight_seed"] = seed
            self.assertEqual(paired([slow, fast], "9", "20", {}, True), [(slow, fast)])
        del fast["flight_seed"]
        self.assertEqual(paired([slow, fast], "9", "20", {}, True), [])

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


class ThreatInputChecks(unittest.TestCase):
    def test_launch_delay_preserves_earlier_mission_inputs(self):
        inputs = [("START.EXE", 100, "--click", "1,2"),
                  ("SETUP.EXE", 900, "--type", "N"),
                  ("START.EXE", 500, "--click", "3,4")]
        delayed = delay_launch(inputs, 20_000_000, 1813)
        self.assertEqual(delayed[:2], inputs[:2])
        self.assertEqual(delayed[2], ("START.EXE", 36_260_500, "--click", "3,4"))
        self.assertEqual(inputs[2][1], 500)
        with self.assertRaises(ValueError):
            delay_launch(inputs, 9_000_000, -1)

    def test_whole_step_holds_keep_the_same_phase_at_both_cpu_speeds(self):
        for ips in (9_000_000, 20_000_000, 33_333_333):
            phase = ips * 11 // 100
            for offset_ms, expected_step in ((0, 0), (1, 1), (100, 1), (125, 1), (126, 2)):
                requested = phase + ips * offset_ms // 1000
                at, hold = step_input(requested, 60, phase, ips)
                self.assertGreaterEqual(at, requested)
                self.assertEqual(at, phase + (expected_step * ips + 7) // 8)
                self.assertEqual(hold, 125)
            self.assertEqual(step_input(phase, 126, phase, ips)[1], 250)

    def test_delayed_and_step_aligned_runs_have_distinct_cache_tags(self):
        tags = {tag_of("strike", "20", 2720), tag_of("strike", "20", 2720, (), 1813),
                tag_of("strike", "20", 2720, (), 0, True)}
        self.assertEqual(len(tags), 3)
        row = dict(speed="20", fixes=["D1REAL"])
        self.assertNotEqual(arm(row), arm(dict(row, pilot_step_aligned=True)))


if __name__ == "__main__":
    unittest.main()
