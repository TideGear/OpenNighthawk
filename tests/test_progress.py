"""Census refresh preserves independently proved interior-routine credit."""
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import progress


class CensusTests(unittest.TestCase):
    def test_proven_interiors_require_their_matched_body(self):
        row = ["START", "0x665B", "96"]
        self.assertTrue(progress.census_row_matched(row, {("START", 0x64E5)}))
        self.assertFalse(progress.census_row_matched(row, set()))
        self.assertFalse(progress.census_row_matched(["END", *row[1:]], {("START", 0x64E5)}))

    def test_span_end_is_exclusive_and_requires_whole_extent(self):
        matched = {("START", 0x64E5)}
        self.assertTrue(progress.census_row_matched(["START", "0x683B", "38"], matched))
        self.assertFalse(progress.census_row_matched(["START", "0x6861", "1"], matched))
        self.assertFalse(progress.census_row_matched(["START", "0x6860", "2"], matched))

    def test_refresh_counts_each_extent_once(self):
        rows = [("0x64E5", 374), ("0x665B", 96), ("0x66BB", 96),
                ("0x671B", 96), ("0x677B", 96), ("0x67DB", 96),
                ("0x683B", 38), ("0x6861", 100)]
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "census.tsv"
            path.write_text("binary\tentry\tsize\n" + "".join(
                f"START\t{entry}\t{size}\n" for entry, size in rows))
            data = {"census": {}}
            with patch.object(progress, "matched_addresses", return_value={
                    ("START", 0x64E5), ("START", 0x665B)}):
                progress.refresh_census(data, path)
            self.assertEqual(data["census"], dict(functions=8, bytes=992,
                                                matched_functions=7, matched_bytes=892))


if __name__ == "__main__":
    unittest.main()
