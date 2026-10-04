"""ROM-free end-to-end checks of saved OPL stream comparisons."""
import pathlib
import struct
import subprocess
import sys
import tempfile
import unittest

TOOL = pathlib.Path(__file__).resolve().parents[1] / "tools/dosbox_compare.py"


class MusicComparison(unittest.TestCase):
    def compare(self, reference, current, window=2):
        with tempfile.TemporaryDirectory() as folder:
            run = pathlib.Path(folder)
            (run / "capture").mkdir()
            registers = sorted({r for r, _ in reference})
            indices = {r: i for i, r in enumerate(registers)}
            commands = bytes(b for r, v in reference for b in (indices[r], v))
            header = b"DBRAWOPL" + struct.pack("<HHIIBBBBBB", 2, 0, len(reference), 0,
                                               0, 0, 0, 254, 255, len(registers))
            (run / "capture/test.dro").write_bytes(header + bytes(registers) + commands)
            (run / "opl.log").write_text("".join(f"{i * 9000} {r:02X} {v:02X}\n"
                                                for i, (r, v) in enumerate(current)))
            return subprocess.run([sys.executable, str(TOOL), "--reuse", str(run),
                                   "--window", str(window)], capture_output=True, text=True)

    def test_exactly_one_alignment_window(self):
        pairs = [(0xA0, 1), (0xB0, 2)]
        result = self.compare(pairs, pairs)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("2 writes in the same order", result.stdout)

    def test_changed_register_value_is_failure(self):
        pairs = [(0xA0, 1), (0xB0, 2), (0xA0, 3), (0xB0, 4)]
        result = self.compare(pairs, pairs[:2] + [(0xA0, 9)] + pairs[3:])
        self.assertEqual(result.returncode, 1)
        self.assertIn("FIRST DIFFERENCE at aligned write 2", result.stdout)

    def test_reordered_writes_are_failure(self):
        pairs = [(0xA0, 1), (0xB0, 2), (0xA0, 3), (0xB0, 4)]
        result = self.compare(pairs, pairs[:2] + pairs[2:][::-1])
        self.assertEqual(result.returncode, 1)
        self.assertIn("FIRST DIFFERENCE", result.stdout)

    def test_no_matching_window_is_failure(self):
        result = self.compare([(0xA0, 1), (0xB0, 2)], [(0xA0, 3), (0xB0, 4)])
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("could not align", result.stderr)


if __name__ == "__main__":
    unittest.main()
