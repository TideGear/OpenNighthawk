"""Fill pattern parity in Python; optionally compare the original executable."""
import argparse
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from drawlist_spans import paint


class Styles(unittest.TestCase):
    def test_patterns(self):
        self.assertEqual(paint(0x1937, 0, 0, [8] * 5), [0x37, 0x19, 0x37, 0x19, 0x37])
        self.assertEqual(paint(0x1937, 1, 0, [8] * 5), [0x19, 0x37, 0x19, 0x37, 0x19])
        self.assertEqual(paint(0xFC37, 0, 1, [8, 9, 10, 11]), [0x37, 9, 0x37, 11])
        self.assertEqual(paint(0xFB37, 0, 0, [8, 9]), [8, 9])

    def test_fine_cells(self):
        for scale in (1, 4, 9):
            for y in range(2 * scale):
                for colour in (0x1937, 0xFC37):
                    got = paint(colour, y, 0, [8] * (4 * scale), scale)
                    coarse = paint(colour, y // scale, 0, [8] * 4)
                    self.assertEqual(got, [v for v in coarse for _ in range(scale)])


def original(executable):
    with tempfile.TemporaryDirectory(prefix="f117-fill-oracle-") as tmp:
        rows = Path(tmp) / "rows.txt"
        subprocess.run([str(executable), "--rows", str(rows)], check=True)
        count = 0
        for line in rows.read_text().splitlines():
            colour, y, x, n, *values = map(int, line.split())
            assert len(values) == 2 * n
            got = paint(colour, y, x, values[:n])
            if got != values[n:]:
                raise AssertionError(f"Python fill differs: {colour:04X}, y={y}, x={x}, n={n}")
            count += 1
        if count < 2000:
            raise AssertionError(f"too few original fill rows: {count}")
        print(f"Python fills: {count} staged rows equal to original bytes")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--original", type=Path)
    args = parser.parse_args()
    result = unittest.TextTestRunner().run(unittest.defaultTestLoader.loadTestsFromTestCase(Styles))
    if not result.wasSuccessful():
        raise SystemExit(1)
    if args.original:
        original(args.original.resolve())
