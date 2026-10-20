"""Damaged-install routes (tools/route_damage.py): the damage is applied to a private copy, the install is never
written, the copy is made once and named after its content, and bad damage fails loudly."""
import os
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import route_damage as rd


class DamageTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.master = os.path.join(self.tmp.name, "install")
        os.makedirs(self.master)
        for name, data in (("A.SS", bytes(range(32))), ("B.PAL", b"palette"), ("C.EXE", b"program")):
            Path(self.master, name).write_bytes(data)
        rd.CACHE = os.path.join(self.tmp.name, "damaged")
        rd.set_master(self.master)

    def tearDown(self):
        self.tmp.cleanup()

    def test_no_damage_leaves_the_arguments_alone(self):
        args = ["--replay", "x.input", "--steps", "5"]
        self.assertEqual(rd.expand(args), args)

    def test_pairs_become_one_data_argument(self):
        args = ["--damage", "remove:B.PAL", "--damage", "poke=2:FFEE:A.SS", "--steps", "5"]
        out = rd.expand(args)
        self.assertEqual(out[:2], ["--steps", "5"])
        self.assertEqual(out[2], "--data")
        data = Path(out[3])
        self.assertFalse((data / "B.PAL").exists())
        self.assertEqual((data / "A.SS").read_bytes(), bytes([0, 1, 0xFF, 0xEE]) + bytes(range(4, 32)))
        self.assertEqual((data / "C.EXE").read_bytes(), b"program")

    def test_the_install_is_never_written(self):
        before = {n: Path(self.master, n).read_bytes() for n in os.listdir(self.master)}
        rd.expand(["--damage", "poke=0:00:A.SS", "--damage", "empty:C.EXE", "--damage", "trunc=3:B.PAL"])
        self.assertEqual({n: Path(self.master, n).read_bytes() for n in os.listdir(self.master)}, before)

    def test_names_ignore_case_and_the_copy_is_made_once(self):
        first = rd.expand(["--damage", "remove:b.pal"])[1]
        stamp = os.path.getmtime(os.path.join(first, ".complete"))
        again = rd.expand(["--damage", "remove:B.PAL"])[1]
        self.assertEqual(first, again)
        self.assertEqual(os.path.getmtime(os.path.join(again, ".complete")), stamp)

    def test_different_damage_gets_a_different_directory(self):
        self.assertNotEqual(rd.expand(["--damage", "remove:B.PAL"])[1], rd.expand(["--damage", "remove:A.SS"])[1])

    def test_bad_damage_fails(self):
        for bad in (["--damage"], ["--damage", "remove"], ["--damage", "remove:NOPE.DAT"],
                    ["--damage", "poke=99:00:A.SS"], ["--damage", "explode:A.SS"]):
            with self.assertRaises(ValueError, msg=bad):
                rd.expand(bad)


if __name__ == "__main__":
    unittest.main()
