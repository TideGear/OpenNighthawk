"""ROM-free checks that route parity also reaches its declared milestones."""
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from run_route import check_route


class RouteChecks(unittest.TestCase):
    def check(self, log):
        with tempfile.TemporaryDirectory() as folder:
            route = Path(folder) / "flight.args"
            route.write_text("# expect-world CU\n# expect-exit VGAME.EXE 0 1000000000\n")
            return check_route(route, log)

    def log(self, world="cu", code=0, duration=1050000000):
        return (f"[file] open '{world}.wld' -> 5 @100 START.EXE\n"
                f"[exec] VGAME.EXE      MZ   entry 30A5:000E @200\n"
                f"[file] open '{world}.3dG' -> 5 @300 VGAME.EXE\n"
                f"[exit] VGAME.EXE terminated with code {code} -> back to F117.COM @{200 + duration}\n")

    def test_completed_flight(self):
        self.assertEqual([], self.check(self.log()))

    def test_equal_early_crashes_cannot_pass(self):
        errors = self.check(self.log(code=129, duration=415960804))
        self.assertEqual(1, len(errors))
        self.assertIn("code 129", errors[0])

    def test_clean_early_exit_cannot_pass(self):
        self.assertTrue(self.check(self.log(duration=500000000)))

    def test_wrong_theatre_cannot_pass(self):
        self.assertEqual(2, len(self.check(self.log(world="lb"))))

    def test_briefing_without_flight_cannot_pass(self):
        self.assertEqual(2, len(self.check("[file] open 'cu.wld' -> 5 @100 START.EXE\n")))


if __name__ == "__main__":
    unittest.main()
