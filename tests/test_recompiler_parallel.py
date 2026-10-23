"""Spawn real translation workers and require the serial output byte for byte."""
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "recompiler"))
from modules import Module
from recomp import read_coverage, translate_modules


def files(folder):
    return {p.name: p.read_bytes() for p in folder.iterdir()}


class ParallelTranslation(unittest.TestCase):
    def test_coverage_still_validates_hash_bounds_and_unrelocated_bytes(self):
        mod = Module("Z.EXE", 0x1234, "exe", bytes.fromhex("b83412c3"), {1})
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "run.cov"
            path.write_text("Z.EXE 1234 0 0 b87856c3\n"  # relocated operand is valid
                            "Z.EXE 1234 0 0 b97856c3\n"  # changed opcode
                            "Z.EXE 1234 0 3 c390\n"      # outside image
                            "Z.EXE 9999 0 0 b83412c3\n") # wrong file
            self.assertEqual(({"Z.EXE": [(0, 0)]}, 2), read_coverage([str(path)], [mod]))

    def compare(self, **options):
        # The relocated MOV and both branch arms exercise live operands;
        # the otherwise disconnected RET is reached only through coverage.
        image = bytes.fromhex("b834128ed885c0740340eb0148c39090c3")
        mods = [Module("Z.EXE", 1, "exe", image, {1}, entries=[(0, 0)]),
                Module("A.EXE", 2, "exe", image, set(), entries=[(0, 0)])]
        coverage = {"Z.EXE": [(0, 15)]}
        sites = {"Z.EXE": [(0, 12)]}
        with tempfile.TemporaryDirectory() as tmp:
            serial, parallel = Path(tmp) / "serial", Path(tmp) / "parallel"
            serial.mkdir()
            parallel.mkdir()
            before = translate_modules(mods, coverage, sites, str(serial), jobs=1, **options)
            after = translate_modules(mods, coverage, sites, str(parallel), jobs=2, **options)
            self.assertGreater(before, 0)
            self.assertEqual(before, after)
            self.assertEqual(files(serial), files(parallel))
            listing = (parallel / "gen_modules.c").read_text()
            self.assertLess(listing.index("&RCM_Z_EXE"), listing.index("&RCM_A_EXE"))

    def test_comments_coverage_isolation_and_relocation(self):
        self.compare()

    def test_no_comments_or_heuristics(self):
        self.compare(comments=False, heuristics=False)

    def test_mutation_output(self):
        self.compare(mutate=("Z.EXE", 0, "ax"))

    def test_worker_failure_does_not_publish_module_list(self):
        mods = [Module(name, 1, "exe", b"\xc3", set(), entries=[(0, 0)])
                for name in ("Z.EXE", "A.EXE")]
        with tempfile.TemporaryDirectory() as tmp:
            with self.assertRaisesRegex(SystemExit, "no translated instruction"):
                translate_modules(mods, {}, {}, tmp, jobs=2, mutate=("A.EXE", 16, "ax"))
            self.assertFalse((Path(tmp) / "gen_modules.c").exists())
            self.assertFalse((Path(tmp) / "gen_relocs.c").exists())


if __name__ == "__main__":
    unittest.main()
