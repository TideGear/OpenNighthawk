"""The Phase 2 denominator is split by tools/census_classify.py, and the rule that protects the score is: when in
doubt a byte stays in the denominator. These tests build a small synthetic program and check each class, the
cases that must NOT be excluded (a short zero run, opcodes that read as text, a byte a route ran), and that
progress.py uses the creditable bytes."""
import collections
import sys
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
sys.path.insert(0, str(ROOT / "recompiler"))
import census_classify as cc
import progress

SLOT = bytes.fromhex("ea00000000")
RET = b"\xc3"


def program():
    """A 0x100-byte image; each case sits at its own row address."""
    image = bytearray(b"\x90" * 0x100)

    def put(at, data):
        image[at:at + len(data)] = data
    put(0x00, b"\x90\x90\x90\xc3")                                       # A: code
    put(0x10, bytes(8))                                                  # B: eight zeros
    put(0x20, bytes(7) + b"\xc3")                                        # C: seven zeros are not a fill
    put(0x30, RET + b"This is some message text")                        # D: text after a RET nothing walks
    put(0x50, RET + bytes.fromhex("3c7374273c6e74513c707460"))           # E: printf compares read as text
    put(0x60, RET + SLOT * 4)                                            # F: four blank slots
    put(0x78, RET + SLOT * 3)                                            # G: three are not a table
    put(0x90, bytes(8))                                                  # H: zeros, one of them run by a route
    put(0xA0, b"\x90\x90\x90\xc3")                                       # I: code kept original
    put(0xB0, RET + bytes.fromhex("0102030405"))                         # J: reviewed data
    return bytes(image)


MODULE = SimpleNamespace(image=program(), off=lambda seg, ip: seg * 16 + ip)
ROWS = [("T", 0x00, 4), ("T", 0x10, 8), ("T", 0x20, 8), ("T", 0x30, 26), ("T", 0x50, 13), ("T", 0x60, 21),
        ("T", 0x78, 16), ("T", 0x90, 8), ("T", 0xA0, 4), ("T", 0xB0, 6)]


def run(covered=None, ran=None, original=(), reviewed=None):
    return cc.classify(ROWS, covered or {}, ran or {}, {"T": MODULE}, set(original), reviewed or {})


class ClassTests(unittest.TestCase):
    def test_credited_bytes_and_the_rest_of_a_code_row(self):
        _, per = run(covered={"T": {0x00, 0x01}})
        self.assertEqual(per[0]["credited"], 2)
        self.assertEqual(per[0]["never_run"], 2)       # decoded as code, no route runs it

    def test_a_zero_fill_of_eight_is_data(self):
        _, per = run()
        self.assertEqual(per[1]["data"], 8)

    def test_seven_zeros_are_not_enough(self):
        _, per = run()
        self.assertEqual(per[2]["data"], 0)

    def test_text_nothing_walks_is_data(self):
        _, per = run()
        self.assertEqual(per[3]["data"], 25)           # all but the RET that ends the walk
        self.assertEqual(per[3]["never_run"], 1)

    def test_opcodes_that_read_as_text_stay_in_the_denominator(self):
        # cmp al,'s' / je / cmp al,'n' / je / cmp al,'p' / je is printable but only 58% letters
        _, per = run()
        self.assertEqual(per[4]["data"], 0)
        self.assertEqual(per[4]["unreached"], 12)

    def test_four_blank_slots_are_a_table_and_three_are_not(self):
        _, per = run()
        self.assertEqual(per[5]["slots"], 20)
        self.assertEqual(per[6]["slots"], 0)
        self.assertEqual(per[6]["unreached"], 15)

    def test_a_byte_a_route_ran_is_never_data(self):
        _, per = run(ran={"T": {0x93}})
        self.assertEqual(per[7]["ran"], 1)
        self.assertEqual(per[7]["data"], 7)

    def test_a_credited_byte_beats_data(self):
        _, per = run(covered={"T": {0x90}})
        self.assertEqual(per[7]["credited"], 1)
        self.assertEqual(per[7]["data"], 7)

    def test_original_code_leaves_only_its_uncredited_bytes(self):
        _, per = run(covered={"T": {0xA0}}, original={("T", 0xA0)})
        self.assertEqual(per[8]["credited"], 1)
        self.assertEqual(per[8]["original"], 3)

    def test_a_reviewed_range_is_data_but_not_over_a_run_byte(self):
        reviewed = {"T": set(range(0xB0, 0xB6))}
        _, per = run(reviewed=reviewed)
        self.assertEqual(per[9]["data"], 6)
        _, per = run(reviewed=reviewed, ran={"T": {0xB2}})
        self.assertEqual((per[9]["ran"], per[9]["data"]), (1, 5))

    def test_every_byte_is_in_exactly_one_class(self):
        totals, per = run(covered={"T": {0x00}}, ran={"T": {0x93}}, original={("T", 0xA0)})
        self.assertEqual(sum(totals.values()), sum(size for _, _, size in ROWS))
        for row, (_, _, size) in zip(per, ROWS):
            self.assertEqual(sum(row.values()), size)


class TableTests(unittest.TestCase):
    def test_the_committed_tables_parse(self):
        original = cc.read_original()
        self.assertIn(("VGAME", 0x01C9B), original)
        data = cc.read_data_table()
        self.assertIn(0x03DF3, data["DSWAP"])
        self.assertTrue(all(isinstance(a, int) for v in data.values() for a in v))

    def test_a_table_line_with_a_range(self):
        with tempfile.TemporaryDirectory() as d:
            path = Path(d) / "t.tsv"
            path.write_text("# comment\nSETUP\t0x100\t4\twhy\n", encoding="utf-8")
            self.assertEqual(cc.read_data_table(str(path))["SETUP"], {0x100, 0x101, 0x102, 0x103})


class ScoreTests(unittest.TestCase):
    def test_the_measure_uses_the_creditable_bytes(self):
        data = {"census": {"matched_bytes": 50, "bytes": 200, "creditable_bytes": 100},
                "phases": {"P2": [{"item": "x", "weight": 1, "done": "census_matched_bytes", "kind": "measured"}]},
                "phase_weights": {"P2": 1}}
        out, _, _ = progress.compute(data)
        self.assertAlmostEqual(out["P2"], 50.0)

    def test_without_the_split_the_whole_census_is_used(self):
        data = {"census": {"matched_bytes": 50, "bytes": 200},
                "phases": {"P2": [{"item": "x", "weight": 1, "done": "census_matched_bytes", "kind": "measured"}]},
                "phase_weights": {"P2": 1}}
        out, _, _ = progress.compute(data)
        self.assertAlmostEqual(out["P2"], 25.0)

    def test_the_classes_must_agree_with_the_credit(self):
        data = {"census": {"matched_bytes": 5, "bytes": 10}}
        totals = collections.Counter(credited=4, data=6)
        with patch.object(cc, "run", return_value=([], totals, [])):
            with self.assertRaises(AssertionError):
                progress.refresh_classes(data, "census", "cover", "data", "routes")
        totals = collections.Counter(credited=5, data=3, original=1, never_run=1)
        with patch.object(cc, "run", return_value=([], totals, [])):
            progress.refresh_classes(data, "census", "cover", "data", "routes")
        self.assertEqual(data["census"]["creditable_bytes"], 6)


if __name__ == "__main__":
    unittest.main()
