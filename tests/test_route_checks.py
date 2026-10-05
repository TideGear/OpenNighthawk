"""ROM-free checks that route parity also reaches its declared milestones."""
from pathlib import Path
import sys
import tempfile
import unittest
from types import SimpleNamespace
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from run_route import check_route, route_args, roster_seed, prepare_roster
from build_recomp import headless


class RouteChecks(unittest.TestCase):
    def test_roster_chain_rejects_cycles_before_running(self):
        with tempfile.TemporaryDirectory() as folder:
            a, b = Path(folder) / "a.args", Path(folder) / "b.args"
            a.write_text("# seed-roster b.args\n")
            b.write_text("# seed-roster a.args\n")
            with self.assertRaisesRegex(ValueError, "cyclic"):
                roster_seed(a)

    def test_seed_earns_roster_and_copies_only_saved_bytes(self):
        with tempfile.TemporaryDirectory() as folder:
            route, seed = Path(folder) / "promotion.args", Path(folder) / "sortie.args"
            route.write_text("# seed-roster sortie.args\n")
            seed.write_text("--steps\n10\n")
            save = Path(folder) / "save"
            save.mkdir()
            def runner(cmd, **kwargs):
                self.assertEqual(str(seed), cmd[2])
                self.assertEqual("interp", cmd[cmd.index("--engine") + 1])
                out = Path(cmd[cmd.index("--out") + 1])
                child_save = out / "fresh"
                child_save.mkdir()
                (child_save / "Roster.Fil").write_bytes(b"r" * 802)
                (child_save / "mission.dat").write_bytes(b"not a career save")
                (out / "save-path.txt").write_text(str(child_save))
                return SimpleNamespace(returncode=0, stdout="", stderr="")
            with patch("run_route.subprocess.run", side_effect=runner):
                prepare_roster(route, "unused", "interp", save, folder)
            self.assertEqual(["Roster.Fil"], [p.name for p in save.iterdir()])
            self.assertEqual(b"r" * 802, (save / "Roster.Fil").read_bytes())

    def test_failed_prerequisite_cannot_seed_a_route(self):
        with tempfile.TemporaryDirectory() as folder:
            route, seed = Path(folder) / "promotion.args", Path(folder) / "sortie.args"
            route.write_text("# seed-roster sortie.args\n")
            seed.write_text("--steps\n10\n")
            with patch("run_route.subprocess.run", return_value=
                       SimpleNamespace(returncode=1, stdout="", stderr="failed milestone")):
                with self.assertRaisesRegex(RuntimeError, "prerequisite route failed"):
                    prepare_roster(route, "unused", "recomp", folder, folder)

    def test_replay_path_is_relative_to_route(self):
        with tempfile.TemporaryDirectory() as folder:
            route = Path(folder) / "landing.args"
            route.write_text("--replay\nlanding.input\n--steps\n1000\n")
            self.assertEqual(route_args(route), ["--replay", str(Path(folder) / "landing.input"),
                                                "--steps", "1000"])

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

    def saved(self, data):
        with tempfile.TemporaryDirectory() as folder:
            route = Path(folder) / "edit.args"
            route.write_text("# expect-save Roster.Fil 2 434845434b00\n")
            if data is not None:
                (Path(folder) / "Roster.Fil").write_bytes(data)
            return check_route(route, "", folder)

    def test_committed_save(self):
        self.assertEqual([], self.saved(b"\0\0CHECK\0"))

    def test_ignored_edit_cannot_pass(self):
        self.assertTrue(self.saved(b"\0\0OLD\0"))

    def test_missing_save_cannot_pass(self):
        self.assertTrue(self.saved(None))

    def test_pipeline_replays_start_with_empty_saves(self):
        with tempfile.TemporaryDirectory() as folder:
            route = Path(folder) / "edit.args"
            route.write_text("--steps\n10\n")
            old = Path(folder) / "runs/edit_recomp/save"
            old.mkdir(parents=True)
            (old / "Roster.Fil").write_bytes(b"an earlier edited pilot")
            saves = []

            def runner(cmd, **kwargs):
                save = Path(cmd[cmd.index("--save") + 1])
                self.assertEqual([], list(save.iterdir()))
                saves.append(save)
                (save / "Roster.Fil").write_bytes(b"this run edited a pilot")
                Path(cmd[cmd.index("--log") + 1]).write_text("")
                return SimpleNamespace(returncode=0, stderr="", stdout=
                                       "stopped at icount 10 (budget); interpreted 0; final hash abcd\n")

            with patch("build_recomp.run", side_effect=runner):
                for _ in range(2):
                    headless("recomp", "unused", folder, "edit", [], [], route)
            self.assertNotEqual(saves[0], saves[1])
            self.assertEqual(b"an earlier edited pilot", (old / "Roster.Fil").read_bytes())

    def opens(self, log):
        with tempfile.TemporaryDirectory() as folder:
            route = Path(folder) / "maintenance.args"
            route.write_text("# expect-open armsscrn.pic START.EXE 2\n")
            return check_route(route, log)

    def test_both_maintenance_visits(self):
        self.assertEqual([], self.opens("[file] open 'ARMSscrn.pic' -> 5 @100 START.EXE\n"
                                        "[file] open 'armsscrn.pic' -> 5 @200 START.EXE\n"))

    def test_only_one_visit_cannot_pass(self):
        self.assertTrue(self.opens("[file] open 'armsscrn.pic' -> 5 @100 START.EXE\n"))

    def test_wrong_program_and_failed_open_cannot_pass(self):
        self.assertTrue(self.opens("[file] open 'armsscrn.pic' -> 5 @100 VGAME.EXE\n"
                                   "[file] open 'armsscrn.pic' -> -1 @200 START.EXE\n"))


if __name__ == "__main__":
    unittest.main()
