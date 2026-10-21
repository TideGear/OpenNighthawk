"""The Phase 2 denominator is split by tools/census_classify.py, and the rule that protects the score is: when in
doubt a byte stays in the denominator. These tests build a small synthetic program and check each class, the
cases that must NOT be excluded (a short zero run, opcodes that read as text, a byte a route ran), and that
progress.py uses the creditable bytes."""
import collections
import hashlib
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
from modules import Module

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


def run(covered=None, ran=None, original=(), reviewed=None, dead=(), dead_slots=None):
    return cc.classify(ROWS, covered or {}, ran or {}, {"T": MODULE}, set(original), reviewed or {}, dead=set(dead), dead_slots=dead_slots)


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

    def test_certified_dead_slot_bytes_leave_the_slots_class_only_in_their_range(self):
        # row F (0x60) holds four blank slots at 0x61..0x74; certify the second and third slot only
        totals, per = run(dead_slots={"T": set(range(0x66, 0x70))})
        self.assertEqual(per[5]["dead"], 10)
        self.assertEqual(per[5]["slots"], 10)
        self.assertEqual(run()[1][5]["dead"], 0)

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

    def test_dead_code_leaves_only_its_never_run_bytes(self):
        _, per = run(dead={("T", 0x00)})
        self.assertEqual((per[0]["dead"], per[0]["never_run"]), (4, 0))
        _, per = run(covered={"T": {0x00}}, ran={"T": {0x01}}, dead={("T", 0x00)})
        self.assertEqual((per[0]["credited"], per[0]["ran"], per[0]["dead"]), (1, 1, 2))   # run and covered bytes stay
        _, per = run()
        self.assertEqual((per[0]["dead"], per[0]["never_run"]), (0, 4))                    # unlisted: stays in the denominator

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


class RouteTests(unittest.TestCase):
    def capture(self, image, records, *, origin=0, relocs=()):
        mod = Module("T.EXE", 0x1234, "exe", image, set(relocs), origin=origin)
        audit = {}
        with tempfile.TemporaryDirectory() as directory:
            Path(directory, "route.cov").write_text(records, encoding="utf-8")
            ran = cc.route_instructions(directory, {"T": mod}, audit)
        return ran.get("T", set()), audit

    def test_the_window_is_not_the_instruction_length(self):
        # PUSH is one byte; MOV word ptr [1234],5678 is six. The same four-byte
        # windows used by the runtime must earn one and six bytes respectively.
        ran, audit = self.capture(bytes.fromhex("55909090c70634127856c3909090"),
                                  "T.EXE 1234 0000 0000 55909090\n"
                                  "T.EXE 1234 0000 0004 c7063412\n")
        self.assertEqual(ran, {0} | set(range(4, 10)))
        self.assertEqual(audit["accepted"], 2)

    def test_wrong_hash_changed_code_and_unpacker_addresses_do_not_count(self):
        ran, audit = self.capture(bytes.fromhex("b83412c3909090"),
                                  "T.EXE 9999 0000 0000 b83412c3\n"
                                  "T.EXE 1234 0000 0000 b87856c3\n"
                                  "T.EXE 1234 1000 0000 b83412c3\n")
        self.assertEqual(ran, set())
        self.assertEqual((audit["identity"], audit["changed"], audit["outside"]), (1, 1, 1))

    def test_relocated_operands_are_allowed_but_changed_opcodes_are_not(self):
        ran, audit = self.capture(bytes.fromhex("b80000c3909090"),
                                  "T.EXE 1234 0000 0000 b83412c3\n"
                                  "T.EXE 1234 0000 0000 b93412c3\n", relocs={1})
        self.assertEqual(ran, {0, 1, 2})
        self.assertEqual((audit["accepted"], audit["changed"]), (1, 1))

    def test_window_bytes_after_the_instruction_are_not_validated(self):
        ran, _ = self.capture(bytes.fromhex("c3909090"), "T.EXE 1234 0000 0000 c3abcdef\n")
        self.assertEqual(ran, {0})

    def test_bound_jump_vectors_keep_all_five_bytes(self):
        ran, audit = self.capture(SLOT * 4, "T.EXE 1234 0000 0005 ea341278\n")
        self.assertEqual(ran, set(range(5, 10)))
        self.assertEqual(audit["bound_slots"], 1)

    def test_a_far_jump_outside_a_vector_table_cannot_change(self):
        ran, audit = self.capture(SLOT, "T.EXE 1234 0000 0000 ea341278\n")
        self.assertEqual(ran, set())
        self.assertEqual(audit["changed"], 1)

    def test_com_origin_is_not_a_byte_offset_or_a_segment_alias(self):
        ran, _ = self.capture(bytes.fromhex("c3909090"),
                              "T.EXE 1234 0010 0000 c3909090\n", origin=0x100)
        self.assertEqual(ran, {0x100})

    def test_duplicates_across_routes_count_once(self):
        record = "T.EXE 1234 0000 0000 c3909090\n"
        ran, audit = self.capture(bytes.fromhex("c3909090"), record * 2)
        self.assertEqual(ran, {0})
        self.assertEqual(audit["accepted"], 1)

    def test_a_truncated_instruction_cannot_extend_beyond_the_image(self):
        ran, audit = self.capture(bytes.fromhex("c7063412"), "T.EXE 1234 0000 0000 c7063412\n")
        self.assertEqual(ran, set())
        self.assertEqual(audit["decode"], 1)

    def test_missing_or_malformed_capture_fails(self):
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaises(ValueError):
                cc.route_instructions(directory, {})
        for record in ("", "T.EXE 1234 0000 0000\n", "T.EXE 1234 0000 0000 c3\n"):
            with self.assertRaises(ValueError):
                self.capture(b"\xc3", record)


class SupplementalTests(unittest.TestCase):
    def test_supplement_adds_executed_bytes_once_without_changing_old_rows(self):
        rows = [("T", 0, 3), ("T", 0x10, 0), ("U", 0, 1)]
        scope = cc.supplemental_scope(rows, {"T": {2, 3, 4, 7}},
                                      {"T": {2, 3, 4, 5}, "U": {0, 1}, "DRIVER.117": {0, 1}})
        self.assertEqual(scope["bytes"], 5)   # T: 3,4,5,7; U: 1
        self.assertEqual(scope["credited"], 3)
        self.assertEqual(scope["by_module"]["T"], dict(bytes=4, credited=3, reviewed_static_bytes=0))
        self.assertEqual(scope["other_modules_executed_bytes"], {"DRIVER.117": 2})
        self.assertEqual(rows[1], ("T", 0x10, 0))

    def test_reviewed_static_code_adds_work_without_inventing_matching_credit(self):
        scope = cc.supplemental_scope([("T", 0, 2)], {"T": {2}}, {"T": {3}}, {"T": {0, 2, 3, 4, 5}})
        self.assertEqual((scope["bytes"], scope["credited"], scope["reviewed_static_bytes"]), (4, 1, 2))


class ReviewedCodeTests(unittest.TestCase):
    IMAGE = bytes.fromhex("55c70634127856c39090909090909090")

    def row(self, *, ip=0, size=8, count=3, digest=None, evidence="reviewed synthetic code"):
        digest = digest or hashlib.sha256(self.IMAGE[ip:ip + size]).hexdigest()
        return "T\t0000\t%04X\t%d\t%d\t%s\t%s\n" % (ip, size, count, digest, evidence)

    def read(self, text):
        mod = Module("T.EXE", 1, "exe", self.IMAGE, set())
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "code.tsv"
            path.write_text(text, encoding="utf-8")
            return cc.reviewed_code_bytes({"T": mod}, path)

    def test_reviewed_range_has_exact_hash_count_and_boundaries(self):
        self.assertEqual(self.read("# review\n" + self.row())["T"], set(range(8)))

    def test_stale_hash_fails(self):
        with self.assertRaisesRegex(ValueError, "hash differs"):
            self.read(self.row(digest="0" * 64))

    def test_wrong_instruction_count_fails(self):
        with self.assertRaisesRegex(ValueError, "count differs"):
            self.read(self.row(count=4))

    def test_partial_instruction_fails(self):
        with self.assertRaisesRegex(ValueError, "does not decode cleanly"):
            self.read(self.row(size=5, count=2))

    def test_overlapping_reviews_fail(self):
        with self.assertRaisesRegex(ValueError, "overlapping"):
            self.read(self.row() + self.row(ip=7, size=2, count=2))

    def test_outside_image_or_segment_fails(self):
        for row in (self.row(size=40), self.row(ip=0xFFFF, size=2)):
            with self.assertRaises(ValueError):
                self.read(row)

    def test_unknown_module_or_missing_evidence_fails(self):
        for row in (self.row().replace("T\t", "UNKNOWN\t"), self.row(evidence="")):
            with self.assertRaises(ValueError):
                self.read(row)


class ScoreTests(unittest.TestCase):
    def test_a_zero_size_row_does_not_hide_executed_code(self):
        scope = cc.supplemental_scope([("T", 0x10, 0)], {"T": {0x10}}, {"T": {0x10, 0x11}})
        self.assertEqual((scope["bytes"], scope["credited"]), (2, 1))

    def test_score_includes_the_supplement_in_both_numerator_and_denominator(self):
        data = {"census": {"matched_bytes": 50, "bytes": 200, "creditable_bytes": 100,
                           "supplemental": {"credited": 10, "bytes": 50}},
                "phases": {"P2": [{"item": "x", "weight": 1, "done": "census_matched_bytes", "kind": "measured"}]},
                "phase_weights": {"P2": 1}}
        out, _, _ = progress.compute(data)
        self.assertAlmostEqual(out["P2"], 40.0)

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
