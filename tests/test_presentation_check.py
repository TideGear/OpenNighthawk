import sys
from pathlib import Path
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from presentation_check import presentation, rerun_presentation, verdict
from present_light_check import refusals

REPLAY = "[present] replay: 4 logic frames, 4 equal to the display at their close, 0 not, 0 before a seed; 0 dropped\n"
PAIRS = "[present] interp: 2 frame pairs, 2 exact at both ends (t = e and 1 - e, e 1e-6, or 1e-9 with a scale), 0 not;\n"
RERUN = ("[present] rerun: 10 in-between pictures drawn by the original on a copy, 2 fell back to interpolation; "
         "at t = 0 the copy drew its frame exactly 3 times, not 0; 1000 instructions a copy\n"
         "[present] isolation: 15 copies checked, 0 changed the real machine, 0 DOS calls blocked\n")


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

    def test_rerun_requires_real_draws_and_isolation(self):
        self.assertEqual(rerun_presentation(RERUN, 2)["rerun_drawn"], 10)
        for text in ("", RERUN.replace("10 in-between", "0 in-between"),
                     RERUN.replace("exactly 3", "exactly 0"), RERUN.replace("15 copies", "1 copies"),
                     RERUN.replace("0 changed", "1 changed"), RERUN.replace("0 DOS", "3 DOS"),
                     RERUN.replace("not 0", "not 1")):
            with self.subTest(text=text), self.assertRaises(ValueError):
                rerun_presentation(text, 2)
        empty = RERUN.replace("10 in-between", "0 in-between").replace("exactly 3", "exactly 0").replace("15 copies", "0 copies")
        self.assertEqual(rerun_presentation(empty, 0)["copies"], 0)

    def test_blocked_copies_must_be_fallbacks(self):
        # a display change loads a picture file in flight: the copy is blocked and replaced by interpolation
        self.assertEqual(rerun_presentation(RERUN.replace("0 DOS", "2 DOS"), 2)["rerun_drawn"], 10)
        with self.assertRaises(ValueError):
            rerun_presentation(RERUN.replace("0 DOS", "3 DOS"), 2)

    def test_machine_copies_cannot_use_the_read_only_light_check(self):
        paths = ["src/present/drawfeed.c", "src/present/drawfeed.h", "src/present/rerun.c", "src/present/rerun.h"]
        self.assertEqual(len(refusals(paths)), len(paths))
        self.assertEqual(refusals(["src/present/hires.c", "docs/presentation.md"]), [])


if __name__ == "__main__":
    unittest.main()
