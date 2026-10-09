"""Oversized compiler regions retain every decoded instruction and operand."""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "recompiler"))
from discover import Region
from recomp import bounded_regions


class BoundedRegions(unittest.TestCase):
    def test_preserves_instructions_operands_segments_and_unique_seeds(self):
        a = Region(0x100, 8, {i: object() for i in range(11)},
                   {i: {i % 3} for i in range(11)})
        b = Region(0x100, 20, {20: object()}, {20: set()})
        pieces = bounded_regions([a, b], 4)
        self.assertEqual([len(r.insns) for r in pieces], [4, 4, 3, 1])
        self.assertEqual(len({(r.seg, r.seed_ip) for r in pieces}), 4)
        self.assertTrue(all(r.seg == 0x100 for r in pieces))
        self.assertTrue(any(r.seed_ip == a.seed_ip for r in pieces))
        self.assertIs(pieces[-1], b)
        for original in (a, b):
            for ip, ins in original.insns.items():
                matches = [r for r in pieces if ip in r.insns]
                self.assertEqual(len(matches), 1)
                self.assertIs(matches[0].insns[ip], ins)
                self.assertIs(matches[0].live[ip], original.live[ip])

    def test_rejects_nonpositive_limit(self):
        with self.assertRaises(ValueError):
            bounded_regions([], 0)


if __name__ == "__main__":
    unittest.main()
