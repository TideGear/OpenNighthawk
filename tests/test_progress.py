"""Census credit is by the bytes named code runs: lockstep and route evidence both count, a walk
stops where the body hands back, partial rows earn their run bytes, shared code counts once, and a
matched routine missing from the coverage stops the refresh."""
import sys
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch

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


if __name__ == "__main__":
    unittest.main()
