"""Normal play repairs defects; parity and legacy replays retain original behavior.

ROM-free process checks: fix listing exits before opening a window or loading
game data. Timing checks record a synthetic infinite-loop COM through dummy SDL
drivers, covering session defaults, overrides and replay metadata precedence.
"""
import argparse
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import unittest

REPAIRS = {"D1", "D1TTL", "D1SLOT", "D1ACCEL", "D2", "D3", "D4", "D5", "D6", "D8", "D11", "D12", "D34", "Q7", "Q12", "Q14", "Q15", "Q16", "Q17", "Q18", "Q19", "Q20", "Q13a", "Q21", "Q22", "Q4", "Q13b", "D18", "Q23", "D101", "Q12b"}
OPTIONS = {"D1REAL", "D1PROX"}
QUIZ = {"QUIZ"}          # START's copy-protection quiz: an option that no switch but its own name turns on
APP = RUNNER = None


def enabled(exe, args, app=False):
    env = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
    command = [str(exe)] + (["--no-config"] if app else []) + args + ["--list-fixes"]
    result = subprocess.run(command, capture_output=True, text=True, env=env, timeout=15)
    if result.returncode:
        raise AssertionError("%s: %s" % (command, result.stdout + result.stderr))
    rows = dict(re.findall(r"^([DQ]\w+)\s+(on|off)\s", result.stdout, re.M))
    if set(rows) != REPAIRS | OPTIONS | QUIZ:
        raise AssertionError("missing or unexpected fix rows: %s" % result.stdout)
    return {name for name, state in rows.items() if state == "on"}


class DefaultsTests(unittest.TestCase):
    def test_original_presentation_overrides_a_remastered_config(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);data=root/"data";data.mkdir();(data/"F117.COM").write_bytes(b"\xeb\xfe")
            config=root/"settings.ini";config.write_text("presentation = remastered\n")
            env={k:v for k,v in os.environ.items() if not k.startswith("F117_")}
            env.update(SDL_VIDEODRIVER="dummy",SDL_AUDIODRIVER="dummy",F117_SETTINGS_TRACE="1")
            result=subprocess.run([str(APP),"--config",str(config),"--data",str(data),"--save",str(root/"save"),
                "--no-record","--presentation","original","--exit-after","50000"],capture_output=True,text=True,env=env,timeout=15)
            self.assertEqual(result.returncode,0,result.stderr)
            self.assertRegex(result.stderr,r"\[settings-start\] child=0 sound=\? presentation=original ")

    def test_session_timing_defaults_overrides_and_replay_compatibility(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            data = root / "data"
            data.mkdir()
            (data / "F117.COM").write_bytes(b"\xeb\xfe")
            config = root / "fallback.ini"
            config.write_text("ips = 20000000\nsim-hz = 8\n")
            legacy = root / "legacy.input"
            legacy.write_text("# f117r-input ips=9000000 time_us=1000000\n")
            modern = root / "modern.input"
            modern.write_text("# f117r-input ips=40000000 time_us=1000000\n"
                              "# f117r-sim-hz 15\n")
            cases = [
                (["--no-config"], 60_000_000, 15),
                (["--no-config", "--sim-hz", "8", "--ips", "20000000"], 20_000_000, 8),
                (["--config", str(config)], 20_000_000, 8),
                (["--config", str(config), "--sim-hz", "15", "--ips", "60000000"], 60_000_000, 15),
                (["--no-config", "--replay", str(legacy)], 9_000_000, 8),
                (["--no-config", "--sim-hz", "8", "--ips", "20000000",
                  "--replay", str(modern)], 40_000_000, 15),
            ]
            for index, (options, ips, cadence) in enumerate(cases):
                with self.subTest(options=options):
                    record = root / ("session-%d.input" % index)
                    result = subprocess.run([str(APP), *options, "--data", str(data),
                        "--save", str(root / ("save-%d" % index)), "--roland", "off",
                        "--engine", "interp", "--record", str(record), "--exit-after", "600000"],
                        capture_output=True, text=True, timeout=15,
                        env=dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy"))
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    headers = record.read_text()
                    self.assertIn("# f117r-input ips=%d " % ips, headers)
                    self.assertEqual("# f117r-sim-hz 15\n" in headers, cadence == 15)

    def test_normal_play_enables_every_fix(self):
        self.assertEqual(enabled(APP, [], app=True), REPAIRS | OPTIONS)

    def test_optional_timing_and_balance_changes_can_be_enabled(self):
        self.assertEqual(enabled(APP, ["--fix", "all"], app=True), REPAIRS | OPTIONS)

    def test_the_quiz_option_is_off_unless_named(self):
        self.assertNotIn("QUIZ", enabled(APP, [], app=True))                    # normal play
        self.assertNotIn("QUIZ", enabled(APP, ["--fix", "all"], app=True))      # `all` is the repairs and options, not the quiz
        self.assertEqual(enabled(APP, ["--fix", "none", "--fix", "QUIZ"], app=True), {"QUIZ"})
        self.assertEqual(enabled(RUNNER, ["--fix", "QUIZ"]), {"QUIZ"})
        self.assertEqual(enabled(APP, ["--fix", "QUIZ", "--fix", "all"], app=True), REPAIRS | OPTIONS | {"QUIZ"})

    def test_retired_q11_is_not_listed_but_still_accepted(self):
        self.assertNotIn("Q11", enabled(APP, [], app=True))                      # not a fix any more (the listing check rejects an extra row)
        self.assertEqual(enabled(APP, ["--fix", "none", "--fix", "Q11"], app=True), set())   # old command lines and logs still run, with no effect

    def test_original_mode_and_custom_subset_override_normal_defaults(self):
        self.assertEqual(enabled(APP, ["--fix", "none"], app=True), set())
        self.assertEqual(enabled(APP, ["--fix", "none", "--fix", "D5"], app=True), {"D5"})

    def test_legacy_replay_does_not_inherit_normal_play_defaults(self):
        self.assertEqual(enabled(APP, ["--replay", "unused.input"], app=True), set())
        self.assertEqual(enabled(APP, ["--replay", "unused.input", "--fix", "D5"], app=True), {"D5"})

    def test_headless_reference_stays_original(self):
        self.assertEqual(enabled(RUNNER, []), set())

    def test_cadence_does_not_enable_repairs_in_reference_runner(self):
        self.assertEqual(enabled(RUNNER, ["--sim-hz", "15"]), set())

    def test_invalid_cadences_fail_before_loading_game(self):
        for exe in (APP, RUNNER):
            for value in ("0", "14", "30", "15junk", "-1"):
                command = [str(exe)] + (["--no-config"] if exe == APP else [])
                result = subprocess.run(command + ["--sim-hz", value, "--list-fixes"],
                                        capture_output=True, text=True, timeout=15)
                self.assertEqual(result.returncode, 2)
                self.assertIn("--sim-hz requires 8 or 15", result.stderr)

    def test_parity_reset_precedes_fix_specific_route_options(self):
        self.assertEqual(enabled(RUNNER, ["--fix", "all", "--fix", "none"]), set())
        self.assertEqual(enabled(RUNNER, ["--fix", "none", "--fix", "D5"]), {"D5"})


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--app", required=True, type=Path)
    parser.add_argument("--runner", required=True, type=Path)
    args, remaining = parser.parse_known_args()
    APP, RUNNER = args.app.resolve(), args.runner.resolve()
    unittest.main(argv=[sys.argv[0]] + remaining)
