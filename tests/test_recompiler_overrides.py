"""The recompiler isolates exactly the addresses the fix table overrides."""
import sys
from pathlib import Path
import tempfile
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "recompiler"))
from recomp import override_sites


class OverrideSites(unittest.TestCase):
    def test_reads_the_override_rows_of_the_real_table(self):
        sites = override_sites()
        self.assertIn((0x0000, 0x6D2E), sites["VGAME.EXE"])          # D5
        self.assertIn((0x0000, 0x8EDC), sites["START.EXE"])          # D4's table
        for ip in (0xE6BE, 0x0F97, 0x0FBD, 0x0F7E, 0x0D5E):         # D34
            self.assertIn((0x0000, ip), sites["VGAME.EXE"])
        # Data corrections share the brace layout but are not code.
        self.assertFalse(any(name.endswith(".WLD") for name in sites))

    def test_only_rows_with_a_function_count(self):
        text = '''
        { "X1", "GAME.EXE", HASH, 0x0001, 0x0020, fix_x, "code" },
        { "X2", "DATA.WLD", 100, 0x0040, 0x00, 0x01 },
        { "X3", "only a description" },
        '''
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "fixes.c"
            path.write_text(text)
            self.assertEqual(override_sites(str(path)), {"GAME.EXE": [(1, 0x20)]})
        self.assertEqual(override_sites(str(Path(folder) / "missing.c")), {})


if __name__ == "__main__":
    unittest.main()
