"""Guard reference observation and sound verdicts against false passes."""
import struct
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from dosbox_flight import find_psp


class ReferenceMemoryTests(unittest.TestCase):
    def image(self, padding=b"\0"):
        ram = bytearray(0x100000)
        at = 0x12340
        ram[at] = ord("M")
        struct.pack_into("<H", ram, at + 1, at // 16 + 1)
        ram[at + 8:at + 16] = b"VGAME".ljust(8, padding)
        ram[at + 16:at + 18] = b"\xcd\x20"
        return ram, at

    def test_live_psp_zero_and_space_padding(self):
        for padding in (b"\0", b" "):
            ram, at = self.image(padding)
            self.assertEqual(find_psp(ram, b"VGAME"), at // 16 + 1)

    def test_reject_stale_or_unowned_or_unaligned_name(self):
        for change in ("owner", "psp", "name"):
            ram, at = self.image()
            ram[at + {"owner": 1, "psp": 16, "name": 13}[change]] ^= 1
            self.assertIsNone(find_psp(ram, b"VGAME"))
        self.assertIsNone(find_psp(b"VGAME   " * 100, b"VGAME"))


try:
    import numpy as np
    from audio_compare import align, compare
except ImportError:
    np = None


@unittest.skipIf(np is None, "audio diagnostics require NumPy")
class SoundVerdictTests(unittest.TestCase):
    def signal(self):
        rng = np.random.default_rng(417)
        return rng.normal(0, .1, 33075)

    def test_identical_and_offset(self):
        signal = self.signal()
        self.assertTrue(compare(signal, signal)["exact_pcm_equal"])
        self.assertEqual(align(np.array([0., 1, 3, 1, 0]), np.array([0., 0, 0, 1, 3, 1, 0])), 2)

    def test_gain_difference_is_failure(self):
        signal = self.signal()
        result = compare(signal, signal * .5)
        self.assertFalse(result["exact_pcm_equal"])
        self.assertAlmostEqual(result["envelope_correlation"], 1)

    def test_silence_cannot_pass(self):
        with self.assertRaises(ValueError): compare(np.zeros(22050), np.zeros(22050))


if __name__ == "__main__":
    unittest.main()
