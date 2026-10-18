"""Normal play repairs defects; parity and legacy replays retain original behavior.

ROM-free process checks: listing fixes exits before opening a window or loading
game data. Actual recorded-session playback is checked separately by app replays.
"""
import argparse
import os
from pathlib import Path
import re
import subprocess
import sys
import unittest

REPAIRS = {"D1", "D1TTL", "D1SLOT", "D1ACCEL", "D2", "D3", "D4", "D5", "D6", "D8", "D11", "D12", "D34"}
OPTIONS = {"D1REAL", "D1PROX"}
APP = RUNNER = None


def enabled(exe, args, app=False):
    env = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
    command = [str(exe)] + (["--no-config"] if app else []) + args + ["--list-fixes"]
    result = subprocess.run(command, capture_output=True, text=True, env=env, timeout=15)
    if result.returncode:
        raise AssertionError("%s: %s" % (command, result.stdout + result.stderr))
    rows = dict(re.findall(r"^(D\w+)\s+(on|off)\s", result.stdout, re.M))
    if set(rows) != REPAIRS | OPTIONS:
        raise AssertionError("missing or unexpected fix rows: %s" % result.stdout)
    return {name for name, state in rows.items() if state == "on"}


class DefaultsTests(unittest.TestCase):
    def test_normal_play_enables_every_fix(self):
        self.assertEqual(enabled(APP, [], app=True), REPAIRS | OPTIONS)

    def test_optional_timing_and_balance_changes_can_be_enabled(self):
        self.assertEqual(enabled(APP, ["--fix", "all"], app=True), REPAIRS | OPTIONS)

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
