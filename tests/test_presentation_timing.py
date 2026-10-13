import sys
from pathlib import Path
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from presentation_timing import check


def capture(interval=16666667):
    lines = ["host_ns icount older newer t replayed",
             "# display renderer=direct3d11 output=3840x2160 refresh=60.000000 vsync=1 scale=9"]
    for i in range(700):
        lines.append(f"{1000000+i*interval} {int(i*interval*.009)} {1+i//6} {2+i//6} {(i%6)/6:.6f} 1")
    return "\n".join(lines)


class Timing(unittest.TestCase):
    def test_target(self):
        result = check(capture())
        self.assertEqual(result["errors"], [])
        self.assertEqual(result["flight_frames"], 700)
        self.assertEqual(check(capture()+"\n"+capture().splitlines()[1])["errors"], [])

    def test_wrong_target_or_pacing(self):
        good = capture()
        for text in (good.replace("3840x2160", "1920x1080"),
                     good.replace("60.000000", "30.000000"),
                     good.replace("vsync=1", "vsync=0"),
                     good.replace("scale=9", "scale=4"),
                     good.replace("direct3d11", "software"), capture(33333333),
                     good.replace("0.166667", "-0.100000")):
            with self.subTest(text=text[:100]):
                self.assertTrue(check(text)["errors"])

    def test_incomplete_or_invalid_capture(self):
        for text in ("", capture().replace("# display", "# missing"),
                     "\n".join(capture().splitlines()[:20]), capture()+"\nbroken",
                     capture()+"\n"+capture().splitlines()[1].replace("scale=9", "scale=4"),
                     capture().replace("0.166667", "nan")):
            with self.subTest(text=text[:100]), self.assertRaises(ValueError):
                check(text)


if __name__ == "__main__":
    unittest.main()
