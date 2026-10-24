"""Census credit is by the bytes named code runs: lockstep and route evidence both count, a walk
stops where the body hands back, partial rows earn their run bytes, shared code counts once, and a
matched routine missing from the coverage stops the refresh."""
import sys
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import mock_open, patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
sys.path.insert(0, str(ROOT / "recompiler"))
import progress
from x86dec import K_JCC, K_RET

NORMAL = 0   # any kind the walk steps through


def code(table):
    """A fake decoder: {ip: (length, kind, target)}; other addresses are 2-byte plain instructions."""
    def insn(module, seg, ip):
        length, kind, target = table.get(ip, (2, NORMAL, 0))
        return SimpleNamespace(length=length, kind=kind, target=target, next_ip=ip + length)
    return insn


class CoverTests(unittest.TestCase):
    # 1000: two-byte op; 1002: JCC to 1008; 1004: op; 1006: RET; 1008: op; 100A: RET.
    PROGRAM = staticmethod(code({0x1002: (2, K_JCC, 0x1008), 0x1006: (1, K_RET, 0), 0x100A: (1, K_RET, 0)}))

    def test_registration_parser_keeps_every_game_module_identity(self):
        source = '\n'.join('"%s", TOKEN, 0x0007, 0x0100' % name for name in
                           ("MISC.EXE", "F117.COM", "ASOUND.117", "ASOUND.LOG", "LZEXE091.STUB"))
        with patch("builtins.open", mock_open(read_data=source)):
            self.assertEqual(progress.matched_addresses(),
                             {(name, 0x170) for name in ("MISC", "F117.COM", "ASOUND.117", "ASOUND.LOG")})

    def test_driver_and_com_proofs_use_distinct_module_keys(self):
        names = ("ASOUND.117", "ASOUND.LOG", "F117.COM", "MGRAPHIC.EXE")
        lines = [f"{name} 0000 0100 1: 0100 |\n" for name in names]
        matched = {(progress.module_key(name), 0x100) for name in names}
        covered = progress.cover_bytes(lines, code({}), matched, set())
        self.assertEqual(covered, {progress.module_key(name): {0x100, 0x101} for name in names})
        with self.assertRaises(SystemExit):
            progress.cover_bytes(lines[1:], code({}), matched, set())

    def test_lockstep_lines_become_bytes_by_linear_address(self):
        covered = progress.cover_bytes(["START.EXE 0000 1000 10: 1000 1004 |\n",
                                        "VGAME.EXE 14C2 0000 3: 0000 |\n"], code({}),
                                       {("START", 0x1000), ("VGAME", 0x14C20)}, set())
        self.assertEqual(covered["START"], {0x1000, 0x1001, 0x1004, 0x1005})
        self.assertEqual(covered["VGAME"], {0x14C20, 0x14C21})

    def test_route_evidence_counts_where_a_route_ran_the_routines_own_code(self):
        routed = {("START", 0, a) for a in (0x1000, 0x1002, 0x1008, 0x100A)}
        covered = progress.cover_bytes(["START.EXE 0000 1000 0: |\n"], self.PROGRAM, {("START", 0x1000)}, routed)
        self.assertEqual(covered["START"], {0x1000, 0x1001, 0x1002, 0x1003, 0x1008, 0x1009, 0x100A})

    def test_a_hand_back_address_ends_the_walk(self):
        routed = {("START", 0, a) for a in range(0x1000, 0x1010, 2)} | {("START", 0, 0x100A)}
        covered = progress.cover_bytes(["START.EXE 0000 1000 5: | 1008\n"], self.PROGRAM, {("START", 0x1000)}, routed)
        self.assertNotIn(0x1008, covered["START"])
        self.assertNotIn(0x100A, covered["START"])
        self.assertIn(0x1004, covered["START"])

    def test_a_route_at_the_same_linear_address_in_another_segment_does_not_count(self):
        routed = {("START", 0x00FF, 0x0010)}   # linear 0x1000 through another segment (an unpacker)
        covered = progress.cover_bytes(["START.EXE 0000 1000 0: |\n"], self.PROGRAM, {("START", 0x1000)}, routed)
        self.assertEqual(covered["START"], set())

    def test_code_no_route_ran_earns_nothing_from_the_walk(self):
        covered = progress.cover_bytes(["START.EXE 0000 1000 0: |\n"], self.PROGRAM, {("START", 0x1000)}, set())
        self.assertEqual(covered["START"], set())

    def test_a_matched_routine_without_coverage_stops_the_refresh(self):
        with self.assertRaises(SystemExit):
            progress.cover_bytes(["START.EXE 0000 1000 10: 1000 |\n"], code({}),
                                 {("START", 0x1000), ("END", 0x0010)}, set())


class RefreshTests(unittest.TestCase):
    def refresh(self, rows, covered):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "census.tsv"
            path.write_text("binary\tentry\tsize\n" + "".join(f"{m}\t{e}\t{s}\n" for m, e, s in rows))
            data = {"census": {}}
            with patch.object(progress, "named_bytes", return_value=covered):
                progress.refresh_census(data, path, "cover", "data", "routes")
            return data["census"]

    def test_whole_partial_and_untouched_rows(self):
        rows = [("START", "0x1000", 4), ("START", "0x1004", 4), ("START", "0x1008", 4)]
        c = self.refresh(rows, {"START": {0x1000, 0x1001, 0x1002, 0x1003, 0x1005}})
        self.assertEqual(c, dict(functions=3, bytes=12, matched_bytes=5,
                                 matched_functions=1, partial_functions=1))

    def test_shared_bytes_count_once_and_modules_stay_apart(self):
        rows = [("START", "0x1000", 4), ("END", "0x1000", 4)]
        c = self.refresh(rows, {"START": set(range(0x0FF0, 0x1010))})
        self.assertEqual((c["matched_bytes"], c["matched_functions"], c["partial_functions"]), (4, 1, 0))


class GameplayScoreTests(unittest.TestCase):
    def test_checklist_scores_reviewed_bytes_and_rejects_inconsistent_counts(self):
        data = {"census": {"matched_bytes": 1, "bytes": 1,
                           "gameplay": {"matched_route_run": 1, "stays_original_reviewed": 0,
                                        "denominator": 1, "checklist": {
                                            "bytes": 4, "with_reviewed_entry": 1,
                                            "without_reviewed_entry": 3}}},
                "phases": {"P2": [{"item": "accounted", "weight": 10,
                                     "done": "accounted_for", "kind": "measured"}]},
                "phase_weights": {"P2": 1}}
        self.assertEqual(progress.compute(data)[0]["P2"], 25)
        k = data["census"]["gameplay"]["checklist"]
        k["without_reviewed_entry"] = 2
        with self.assertRaises(ValueError):
            progress.compute(data)
        k.update(bytes=0, with_reviewed_entry=0, without_reviewed_entry=0)
        self.assertEqual(progress.compute(data)[0]["P2"], 100)

    PHASES = {"P2": [{"item": "named", "weight": 1, "done": "census_matched_bytes", "kind": "measured"},
                     {"item": "matched", "weight": 3, "done": "gameplay_score", "kind": "measured"}]}

    def test_the_matched_item_uses_the_gameplay_score_and_the_naming_item_the_previous_measure(self):
        data = {"census": {"matched_bytes": 50, "bytes": 200, "creditable_bytes": 100,
                           "supplemental": {"credited": 10, "bytes": 50},
                           "gameplay": {"matched_route_run": 70, "stays_original_reviewed": 10, "denominator": 100}},
                "phases": self.PHASES, "phase_weights": {"P2": 1}}
        out, rows, _ = progress.compute(data)
        self.assertEqual([round(r[3], 4) for r in rows], [0.4, 0.8])
        self.assertAlmostEqual(out["P2"], 100 * (0.4 + 3 * 0.8) / 4)
        self.assertEqual(progress.title(out), "(P2 %.2f%%, All %.2f%%)" % (out["P2"], out["All"]))

    def refresh(self, reference_text, ran, covered, scope_bytes, occurrences):
        import census_classify as cc
        data = {"census": {"creditable_bytes": occurrences, "supplemental": {"bytes": 0}}}
        detail = dict(rows=[("VGAME", 0x00010, 224), ("VGAME", 0x01C9B, 102)], covered=covered, ran=ran,
                      scope_bytes=scope_bytes)
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "reference.tsv"
            path.write_text("# header\n" + reference_text, encoding="utf-8")
            with patch.object(cc, "read_original", return_value={("VGAME", 0x01C9B)}):
                progress.refresh_gameplay(data, detail, str(path))
        return data["census"]["gameplay"]

    def test_refresh_reviews_the_reference_table_and_cross_checks_the_pieces(self):
        timer = set(range(0x01C9B, 0x01C9B + 97))
        ran = {"VGAME": timer | {0x100, 0x101}, "LZEXE091.STUB": {0}}
        g = self.refresh("", ran, {"VGAME": {0x100, 0x200}}, {"VGAME": {0x100: "credited", 0x200: "credited"}}, 3)
        self.assertEqual((g["denominator"], g["matched_route_run"], g["stays_original_reviewed"], g["fixture_only"]),
                         (99, 1, 0, 1))
        self.assertEqual((g["original_awaiting_review"], g["crosscheck"]["previous_scope_shared_row_bytes"]), (97, 1))
        g = self.refresh("VGAME\t1C9B\t102\ttimer_isr\tthe timer interrupt\tstays-original\tSTI-first handler\tchecked\n",
                         ran, {"VGAME": {0x100}}, {}, 0)
        self.assertEqual((g["stays_original_reviewed"], g["original_awaiting_review"], g["remaining"]), (97, 0, 1))
        with self.assertRaises(ValueError):    # a declined row (not in census-original.tsv) cannot stay original
            self.refresh("VGAME\t0010\t4\tmain\tmain\tstays-original\tC start-up\tchecked\n", ran, {}, {}, 0)


if __name__ == "__main__":
    unittest.main()
