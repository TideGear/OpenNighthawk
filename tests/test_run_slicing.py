"""One long run must match fine slicing (docs/architecture.md: a run is a
pure function of the program, its files, and the inputs with the clock
counts at which they arrived - "a run does not depend on how the host
slices it").

Regression: nested machine runs (a matched routine's guest_call) used to
return only at the outer limit, because recomp_run batched past the armed
trap to stop_at. Each missed return nested one C level deeper per model
edge; any single machine_api run over dense flight drawing longer than a
few million clocks died with a native stack overflow, while 10 ms
headless slices and 1/5 s pilot steps survived. A single strike-replay run
from boot overflowed at clock 2344871415; after the trap poll in
recomp_run it completes, with the same hash as the sliced run.

Needs the game install: set F117R_TEST_DATA to its directory (holds
F117.COM). Skipped without it. About a minute when run.
"""
import os
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from machine_api import Machine

ROOT = Path(__file__).resolve().parents[1]
DATA = Path(os.environ.get("F117R_TEST_DATA", ""))
REPLAY = ROOT / "tools" / "routes" / "strike.input"
TARGET = 2_360_000_000


def needs_data(test):
    return unittest.skipIf(not (DATA / "F117.COM").is_file(),
                           "game install required (set F117R_TEST_DATA)")(test)


def replay_events():
    lines = REPLAY.read_text().splitlines()
    assert lines[0] == "# f117r-input ips=9000000 time_us=700000000000000", lines[0]
    events = []
    for line in lines[1:]:
        parts = line.split()
        if parts and not parts[0].startswith("#"):
            events.append(parts)
    return events


class RunSlicing(unittest.TestCase):
    @needs_data
    def test_long_run_matches_sliced_run(self):
        events = replay_events()
        with tempfile.TemporaryDirectory() as scratch:
            sliced = self._run(Path(scratch) / "sliced", events, TARGET, chunked=True)
            single = self._run(Path(scratch) / "single", events, TARGET, chunked=False)
        self.assertEqual(sliced, single)

    def _run(self, out, events, target, chunked):
        out.mkdir(parents=True)
        save = tempfile.mkdtemp(dir=str(out))
        with Machine(str(DATA), save, log=str(out / "run.log"), engine="recomp") as m:
            for parts in events:
                if int(parts[1]) >= target:
                    break
                if parts[0] == "K":
                    m.key(int(parts[1]), int(parts[2], 16))
                else:
                    m.mouse(int(parts[1]), *map(int, parts[2:5]))
            while m.clock < target:
                if chunked:
                    step = m.ips // 5 if m.program == "VGAME.EXE" else 90000
                    self.assertEqual(m.run_until(min(m.clock + step, target)),
                                     Machine.SLICE)
                else:
                    self.assertEqual(m.run_until(target), Machine.SLICE)
            self.assertEqual(m.clock, target)
            return f"{m.hash:016x}"


if __name__ == "__main__":
    unittest.main()
