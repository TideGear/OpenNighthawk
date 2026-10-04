"""Exercise the host API with a tiny, freely authored DOS program."""
import re
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from machine_api import Machine, RouteInputs, library_path


class MachineTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.path = Path(self.temp.name)
        # BIOS read key; store AX at DS:0200; terminate with code zero.
        (self.path / "F117.COM").write_bytes(bytes.fromhex("b400cd16a30002b8004ccd21"))

    def tearDown(self):
        self.temp.cleanup()

    def open(self, **kwargs):
        return Machine(self.path, self.path / "save", **kwargs)

    def test_lifetime_and_failed_boot(self):
        with self.assertRaises(RuntimeError):
            Machine(self.path / "missing", self.path / "save")
        with self.open() as m:
            with self.assertRaisesRegex(RuntimeError, "one live machine"):
                self.open()
            self.assertEqual(m.program, "F117.COM")
            self.assertEqual(m.start, 0)
        with self.assertRaisesRegex(RuntimeError, "closed"):
            m.read8(0)
        with self.open():
            pass

    def test_observation_and_normal_keyboard(self):
        with self.open() as m:
            address = (m.psp << 4) + 0x100
            original = m.hash
            self.assertEqual(m.read8(address), 0xB4)
            self.assertEqual(m.read16(address), 0x00B4)
            self.assertEqual(m.read32(address), 0x16CD00B4)
            self.assertEqual(m.read16(address + 0x100000), 0x00B4)
            self.assertEqual(m.hash, original)
            result = (m.psp << 4) + 0x200
            self.assertEqual(m.run_until(1000), Machine.SLICE)
            with self.assertRaises(ValueError):
                m.key(0, 0x1E)
            with self.assertRaises(ValueError):
                m.run_until(0)
            m.type(m.clock + 1000, "a")
            self.assertEqual(m.run_until(10_000_000), Machine.EXITED)
            self.assertEqual(m.read16(result), 0x1E61)

    def test_record_replays_in_headless_runner(self):
        recording = self.path / "input.log"
        with self.open() as m:
            m.record(recording)
            with self.assertRaises(RuntimeError):
                m.record(self.path / "second.log")
            m.mouse(100, 40, 50)
            m.type(2000, "A")
            result = (m.psp << 4) + 0x200
            self.assertEqual(m.run_until(10_000_000), Machine.EXITED)
            self.assertEqual(m.read16(result), 0x1E41)
            clock, state = m.clock, f"{m.hash:016x}"
            m.screen(self.path / "screen.ppm")
        self.assertTrue((self.path / "screen.ppm").read_bytes().startswith(b"P6"))
        dll = library_path()
        exe = dll.parent / ("f117run.exe" if sys.platform == "win32" else "f117run")
        output = subprocess.check_output([str(exe), "--data", str(self.path),
            "--save", str(self.path / "replay-save"), "--replay", str(recording),
            "--steps", "10000000"], text=True)
        match = re.search(r"stopped at icount (\d+).*final hash ([0-9a-f]+)", output)
        self.assertIsNotNone(match, output)
        self.assertEqual(match.groups(), (str(clock), state))

    def test_route_inputs_and_missed_deadline(self):
        with self.open() as m:
            route = RouteInputs(["--move", "F117.COM+100:10,20",
                                 "--type", "F117.COM+2000:~30:a"])
            route.poll(m)
            self.assertEqual(route.pending, [])
            result = (m.psp << 4) + 0x200
            self.assertEqual(m.run_until(10_000_000), Machine.EXITED)
            self.assertEqual(m.read16(result), 0x1E61)
        with self.open() as m:
            m.run_until(1000)
            with self.assertRaisesRegex(ValueError, "past"):
                RouteInputs(["--type", "F117.COM+100:a"]).poll(m)

    def test_full_queue_is_reported(self):
        with self.open() as m:
            for _ in range(4096):
                m.key(10000, 0x1E)
            with self.assertRaisesRegex(RuntimeError, "cannot queue input"):
                m.key(10000, 0x9E)


if __name__ == "__main__":
    unittest.main()
