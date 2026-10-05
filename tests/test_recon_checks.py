"""Photo credit requires the original exposure, event and intact target."""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from recon_pilot import recon_errors, control


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

    def test_slow_photo_approach_and_cruise_recover_power(self):
        class Input:
            clock = 100
            ips = 9000000
            def __init__(self): self.keys = []
            def type(self, at, text, **kwargs): self.keys.append(text)
        # The independent flight lost altitude at 44% power after its first
        # photo. Recover speed even when the next target is still far away.
        state = dict(target_x=0, target_y=0, x=0, y=1000, heading=0,
            roll=0, altitude=2500, trim=0, pitch=0, lock=1, target=1,
            flags=1, display=19, mode=2, weapon=16, bay_switch=1,
            cue=0, photos=0, throttle=44, speed=220)
        for distance in (1000, 4000):
            inputs = Input()
            control(inputs, dict(state, target_range=distance), 0)
            self.assertIn("=", inputs.keys)
            self.assertNotIn("-", inputs.keys)

    def test_aim_at_close_target_before_designating(self):
        class Input:
            clock = 100
            ips = 9000000
            def __init__(self): self.keys = []
            def type(self, at, text, **kwargs): self.keys.append(text)
        inputs = Input()
        state = dict(target_x=0, target_y=0, x=0, y=300, heading=0,
            roll=0, altitude=2500, trim=0, pitch=0, lock=16, target=2,
            target_range=300, flags=1, display=19, mode=2, weapon=16,
            bay_switch=1, cue=0, photos=0, throttle=60, speed=300)
        control(inputs, state, 1)
        self.assertIn(r"\U", inputs.keys)

    def test_designation_ray_and_photo_camera_have_different_pitch(self):
        class Input:
            clock = 100
            ips = 9000000
            def __init__(self): self.keys = []
            def type(self, at, text, **kwargs): self.keys.append(text)
        # At this range the nose should point down to acquire the target,
        # while the mounted camera needs a slightly positive aircraft pitch.
        state = dict(target_x=0, target_y=0, x=0, y=1000, heading=0,
            roll=0, altitude=2500, trim=0, pitch=0, target=2,
            target_range=1000, flags=1, display=19, mode=2, weapon=16,
            bay_switch=1, cue=0, photos=0, throttle=60, speed=300)
        acquisition, photo = Input(), Input()
        control(acquisition, dict(state, lock=16), 1)
        control(photo, dict(state, lock=2), 1)
        self.assertIn(r"\U", acquisition.keys)
        self.assertIn(r"\D", photo.keys)


if __name__ == "__main__":
    unittest.main()
