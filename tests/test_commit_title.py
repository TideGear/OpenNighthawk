"""Scored titles must reject omitted, stale and malformed prefixes."""
import sys
from pathlib import Path
import unittest
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'tools'))
from check_commit_title import validate, current_prefix


class CommitTitles(unittest.TestCase):
    def test_score_comes_from_the_committing_worktree(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root/'tools').mkdir()
            prefix = '(P1 100.00%, P2 10.00%, P3 20.00%, All 30.00%)'
            (root/'tools/progress.py').write_text(f'print({prefix!r})')
            self.assertEqual(current_prefix(root), prefix)
            (root/'tools/progress.py').write_text('print("missing score")')
            with self.assertRaises(ValueError):
                current_prefix(root)

    def test_current_score_with_description_and_body(self):
        validate('(P1 100.00%, P2 96.93%, P3 93.84%, All 97.51%) Fix coast\n\nEvidence here.',
                 '(P1 100.00%, P2 96.93%, P3 93.84%, All 97.51%)')

    def test_missing_stale_and_empty_titles_fail(self):
        prefix = '(P1 100.00%, P2 96.93%, P3 93.84%, All 97.51%)'
        for title in ['', '\n'+prefix+' Fix coast', 'Fix coast', prefix, prefix+' ',
                      prefix.replace('93.84', '93.83')+' Fix coast', '**'+prefix+'** Fix coast',
                      'Fix coast '+prefix]:
            with self.subTest(title=title), self.assertRaises(ValueError):
                validate(title, prefix)


if __name__ == '__main__':
    unittest.main()
