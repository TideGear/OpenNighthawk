import sys
from pathlib import Path
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from presentation_check import presentation, verdict

REPLAY = "[present] replay: 4 logic frames, 4 equal to the display at their close, 0 not, 0 before a seed; 0 dropped\n"
PAIRS = "[present] interp: 2 frame pairs, 2 exact at both ends (t = e and 1 - e, e 1e-6, or 1e-9 with a scale), 0 not;\n"


class Verdict(unittest.TestCase):
    def test_exact(self):
        self.assertEqual(presentation(REPLAY + PAIRS, True), dict(frames=4, pairs=2, dropped=0))

    def test_missing_or_bad_summary(self):
        for text in ("", REPLAY, PAIRS, (REPLAY + PAIRS).replace("0 dropped", "1 dropped"),
                     (REPLAY + PAIRS).replace("0 before a seed", "1 before a seed"),
                     (REPLAY + PAIRS).replace("4 equal", "3 equal"),
                     (REPLAY + PAIRS).replace("2 exact", "1 exact"),
                     (REPLAY + PAIRS).replace("0 not;", "1 not;")):
            with self.subTest(text=text), self.assertRaises(ValueError):
                presentation(text, True)

    def test_empty_flight_cannot_pass(self):
        text = REPLAY.replace("4 ", "0 ") + PAIRS.replace("2 ", "0 ")
        self.assertEqual(presentation(text, False)["frames"], 0)
        with self.assertRaises(ValueError):
            presentation(text, True)

    def test_guest_verdict(self):
        short = "stopped at icount 1000000 test final hash 0123"
        self.assertEqual(verdict(short), (("1000000", "0123"), []))
        for text in ("", short + "\n" + short, short.replace("1000000", "50000000")):
            with self.subTest(text=text), self.assertRaises(ValueError):
                verdict(text)
        full = "[hash] 50000000 0123 VGAME.EXE\n" + short.replace("1000000", "50000000")
        self.assertEqual(len(verdict(full)[1]), 1)


if __name__ == "__main__":
    unittest.main()
