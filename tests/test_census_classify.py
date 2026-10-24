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


def run(covered=None, ran=None, original=(), reviewed=None, dead=(), dead_ranges=None):
    return cc.classify(ROWS, covered or {}, ran or {}, {"T": MODULE}, set(original), reviewed or {}, dead=set(dead), dead_ranges=dead_ranges)


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
        totals, per = run(dead_ranges={"T": set(range(0x66, 0x70))})
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
    def test_all_game_files_enter_scope_without_crediting_unmatched_drivers(self):
        ran = {"T": {0, 1}, "F117.COM": {0x100, 0x101}, "ASOUND.117": {0, 1},
               "ASOUND.LOG": {0, 1}, "MGRAPHIC": {0}, "LZEXE091.STUB": {0}, "UNKNOWN": {0}}
        covered = {"ASOUND.117": {1, 2}}
        reviewed = {"NSOUND.117": {0x6E}}
        scope = cc.supplemental_scope([("T", 0, 1)], covered, ran, reviewed)
        self.assertEqual((scope["bytes"], scope["credited"], scope["reviewed_static_bytes"]), (10, 2, 1))
        self.assertEqual(scope["by_module"]["ASOUND.LOG"]["credited"], 0)
        self.assertEqual(scope["by_module"]["F117.COM"]["bytes"], 2)
        self.assertEqual(scope["by_module"]["NSOUND.117"]["bytes"], 1)
        self.assertEqual(scope["by_module"]["RSOUND.117"]["bytes"], 0)
        self.assertEqual(scope["other_modules_executed_bytes"], {"LZEXE091.STUB": 1, "UNKNOWN": 1})
        self.assertEqual(len(cc.modules_scope()), 17)

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


HEADER = "# reviewed reference entries\n"


def entry(module, start, size, status, why="why", note="second reader checked name and purpose"):
    return "%s\t%X\t%d\tname\tpurpose\t%s\t%s\t%s\n" % (module, start, size, status, why, note)


class ReferenceTableTests(unittest.TestCase):
    def read(self, text):
        with tempfile.TemporaryDirectory() as d:
            path = Path(d) / "reference.tsv"
            path.write_text(text, encoding="utf-8")
            return cc.read_reference_reviewed(str(path), modules={"T", "VGAME"})

    def test_the_committed_table_parses_and_every_original_entry_names_its_design_rule(self):
        entries = cc.read_reference_reviewed()
        for e in entries:
            if e["status"] == "stays-original":
                self.assertTrue(e["why"].lower().startswith(tuple(r.lower() for r in cc.DESIGN_RULES)), (e["module"], e["start"]))
            self.assertTrue(e["note"].strip(), (e["module"], e["start"]))

    def test_an_entry_parses(self):
        [e] = self.read(HEADER + entry("VGAME.EXE", 0x1138C, 33, "stays-original"))
        self.assertEqual((e["module"], e["start"], e["bytes"], e["status"]), ("VGAME", 0x1138C, 33, "stays-original"))

    def test_one_status_per_range(self):
        with self.assertRaisesRegex(ValueError, "overlaps"):
            self.read(entry("T", 0x10, 4, "needs-route") + entry("T", 0x13, 2, "probable-dead"))
        with self.assertRaisesRegex(ValueError, "exactly one of"):
            self.read(entry("T", 0x10, 4, "needs-route | probable-dead"))
        self.assertEqual(len(self.read(entry("T", 0x10, 4, "needs-route") + entry("T", 0x14, 2, "data"))), 2)

    def test_an_unreviewed_or_malformed_entry_fails(self):
        for text in (entry("T", 0x10, 4, "data", note=" "), entry("T", 0x10, 4, "data").replace("\tname", ""),
                     entry("UNKNOWN", 0x10, 4, "data"), entry("T", 0x10, 0, "data")):
            with self.assertRaises(ValueError):
                self.read(text)


class GameplayTests(unittest.TestCase):
    """Synthetic module T: census rows 0x10 (design-rule original, in census-original.tsv) and 0x20 (declined:
    a census row that is not in census-original.tsv)."""
    ROWS = [("T", 0x10, 4), ("T", 0x20, 4)]
    ORIGINAL = {("T", 0x10)}

    def score(self, covered=None, ran=None, scope=None, entries=()):
        entries = list(entries)
        cc.check_design_rule(entries, self.ROWS, self.ORIGINAL)
        return cc.gameplay(covered or {}, ran or {}, scope or {}, cc.original_bytes(self.ROWS, self.ORIGINAL),
                           entries, {"T"})

    @staticmethod
    def stays(start, size, why="C start-up copy"):
        return dict(module="T", start=start, bytes=size, status="stays-original", why=why, name="n", purpose="p", note="r")

    def test_route_run_matched_bytes_count_and_fixture_only_bytes_do_not(self):
        g = self.score(covered={"T": {0, 1, 2, 0x40, 0x41}}, ran={"T": set(range(8))})
        self.assertEqual((g["denominator"], g["matched_route_run"], g["fixture_only"], g["remaining"]), (8, 3, 2, 5))
        self.assertAlmostEqual(g["score"], 3 / 8)

    def test_design_rule_original_counts_only_with_a_reviewed_entry(self):
        ran = {"T": set(range(0x10, 0x14)) | {0}}
        g = self.score(ran=ran)
        self.assertEqual((g["stays_original_reviewed"], g["original_awaiting_review"], g["remaining"]), (0, 4, 5))
        g = self.score(ran=ran, entries=[self.stays(0x10, 4)])
        self.assertEqual((g["stays_original_reviewed"], g["original_awaiting_review"], g["remaining"]), (4, 0, 1))
        self.assertAlmostEqual(g["score"], 4 / 5)

    def test_a_matched_byte_inside_original_code_counts_once(self):
        g = self.score(covered={"T": {0x10}}, ran={"T": set(range(0x10, 0x14))}, entries=[self.stays(0x10, 4)])
        self.assertEqual((g["matched_route_run"], g["stays_original_reviewed"], g["remaining"]), (1, 3, 0))

    def test_a_declined_routine_is_not_counted(self):
        with self.assertRaisesRegex(ValueError, "outside docs/census-original.tsv"):
            self.score(ran={"T": set(range(0x20, 0x24))}, entries=[self.stays(0x20, 4)])
        with self.assertRaisesRegex(ValueError, "outside docs/census-original.tsv"):
            self.score(entries=[self.stays(0x12, 0x10)])          # spills from the original row into the declined one
        g = self.score(ran={"T": set(range(0x20, 0x24))},
                       entries=[dict(self.stays(0x20, 4), status="needs-route")])
        self.assertEqual((g["stays_original_reviewed"], g["remaining"]), (0, 4))

    def test_original_code_outside_the_census_must_name_its_design_rule(self):
        with self.assertRaisesRegex(ValueError, "design rule"):
            self.score(ran={"T": {0x80}}, entries=[self.stays(0x80, 2, why="hard to prove")])
        g = self.score(ran={"T": {0x80, 0x81}}, entries=[self.stays(0x80, 2, why="STI-first handler: driver timer")])
        self.assertEqual(g["stays_original_reviewed"], 2)

    def test_program_launcher_is_a_design_rule(self):
        # owner, 23 Oct 2026: F117.COM's launcher body only runs the other programs and tests their exit codes
        g = self.score(ran={"T": {0x80, 0x81}},
                       entries=[self.stays(0x80, 2, why="program launcher: runs the other programs in order")])
        self.assertEqual(g["stays_original_reviewed"], 2)

    def test_hardware_wait_loop_is_a_design_rule(self):
        # owner, 23 Oct 2026: a calibration or wait loop that no lockstep state can complete may stay original
        g = self.score(ran={"T": {0x80, 0x81}},
                       entries=[self.stays(0x80, 2, why="hardware wait loop: polls the timer until it changes")])
        self.assertEqual(g["stays_original_reviewed"], 2)

    def test_original_code_inside_a_census_original_row_must_also_name_its_design_rule(self):
        # a census-original row can be wider than its design-rule routine (a C start-up row also holds the run
        # time's divide-error entry): the row alone is not evidence
        with self.assertRaisesRegex(ValueError, "design rule"):
            self.score(ran={"T": {0x12}}, entries=[self.stays(0x12, 1, why="divide-error entry inside the start-up row")])

    def test_rejected_route_records_are_not_in_the_denominator(self):
        mod = Module("T.EXE", 0x1234, "exe", bytes.fromhex("b83412c3909090"), set())
        with tempfile.TemporaryDirectory() as directory:
            Path(directory, "route.cov").write_text("T.EXE 1234 0000 0000 b87856c3\n"     # changed code: rejected
                                                    "T.EXE 9999 0000 0003 c3909090\n"     # wrong file: rejected
                                                    "T.EXE 1234 0000 0003 c3909090\n", encoding="utf-8")
            ran = cc.route_instructions(directory, {"T": mod})
        g = self.score(covered={"T": {0, 1, 2, 3}}, ran=ran)
        self.assertEqual((g["denominator"], g["matched_route_run"], g["fixture_only"]), (1, 1, 3))

    def test_the_checklist_splits_previous_scope_bytes_outside_the_denominator(self):
        scope = {"T": {0x30: "never_run", 0x31: "never_run", 0x32: "credited", 0x05: "ran"}}
        entries = [dict(self.stays(0x30, 1), status="probable-dead")]
        g = self.score(ran={"T": {0x05, 0x06}}, scope=scope, entries=entries)
        self.assertEqual(g["checklist"]["bytes"], 3)
        self.assertEqual((g["checklist"]["with_reviewed_entry"], g["checklist"]["without_reviewed_entry"]), (1, 2))
        self.assertEqual(g["checklist"]["by_class"]["never_run"], dict(with_reviewed_entry=1, without_reviewed_entry=1))
        self.assertEqual(g["crosscheck"], dict(previous_scope_distinct=4, denominator_in_previous_scope=1,
                                               denominator_outside_previous_scope=1))

    def test_the_pieces_are_disjoint_and_add_up_to_the_denominator(self):
        g = self.score(covered={"T": {0, 0x11}}, ran={"T": {0, 1, 0x10, 0x11, 0x12, 0x20}},
                       entries=[self.stays(0x10, 4)])
        self.assertEqual(g["matched_route_run"] + g["stays_original_reviewed"] + g["remaining"], g["denominator"])
        self.assertEqual((g["matched_route_run"], g["stays_original_reviewed"], g["remaining"]), (2, 2, 2))

    def test_the_previous_scope_collects_distinct_in_scope_bytes(self):
        scope = {}
        cc.classify(ROWS, {"T": {0x00}}, {"T": {0x93}}, {"T": MODULE}, {("T", 0xA0)}, {}, scope_bytes=scope)
        self.assertEqual(scope["T"][0x00], "credited")
        self.assertEqual(scope["T"][0x93], "ran")
        self.assertNotIn(0x10, scope["T"])     # data
        self.assertNotIn(0xA0, scope["T"])     # original
        cc.supplemental_scope(ROWS, {"T": {0xF0}}, {"T": {0xF1}}, {"T": {0xF2}}, scope_bytes=scope)
        self.assertEqual([scope["T"][b] for b in (0xF0, 0xF1, 0xF2)], ["supp-credited", "supp-ran", "supp-reviewed-static"])


if __name__ == "__main__":
    unittest.main()
