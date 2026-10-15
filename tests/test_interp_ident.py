"""The presentation's identity pairing (interp_frame.align_batches, mirroring interp.c): synthetic batches only."""
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import os
os.environ["F117_PRESENT_IDENT"] = "1"   # the experiment is off by default
import interp_frame as I


def batch(idx, model, cell, x, nverts=4, chunk=0):
    """A batch with an identity record (scene_capture.h 'I' fields) and nverts vertices at screen x."""
    b = I.Batch(idx)
    v = [0] * 22
    v[1], v[6] = model, 4
    v[11], v[12], v[13], v[14], v[15] = cell[0], 0, cell[1], 0, 0
    b.ident, b.chunk = v, chunk
    b.verts = [[0, 0, 1 << 20, x + k, 50, 0, 0xC6B4, 0] for k in range(nverts)]
    return b


class IdentityPairing(unittest.TestCase):
    def test_far_move_pairs_by_identity(self):
        # 200 px is past the motion limit: without identity the batches would not pair.
        self.assertEqual(I.align_batches([batch(0, 7, (1, 2), 0)], [batch(0, 7, (1, 2), 200)]), [(0, 0)])
        saved = I.ident_key
        try:
            I.ident_key = lambda b: None
            self.assertEqual(I.align_batches([batch(0, 7, (1, 2), 0)], [batch(0, 7, (1, 2), 200)]), [])
        finally:
            I.ident_key = saved

    def test_swapped_order_pairs_both(self):
        A = [batch(0, 7, (1, 2), 0), batch(1, 9, (3, 4), 100)]
        B = [batch(0, 9, (3, 4), 100), batch(1, 7, (1, 2), 0)]
        self.assertEqual(I.align_batches(A, B), [(0, 1), (1, 0)])

    def test_duplicates_and_detail_changes_are_not_forced(self):
        # Two of the same object in a cell are ambiguous; another vertex count is another mesh.
        self.assertEqual(I.ident_pairs([batch(0, 7, (1, 2), 0), batch(1, 7, (1, 2), 40)],
                                       [batch(0, 7, (1, 2), 0)]), [])
        self.assertEqual(I.ident_pairs([batch(0, 7, (1, 2), 0, 4)], [batch(0, 7, (1, 2), 0, 6)]), [])
        self.assertEqual(I.ident_pairs([batch(0, 7, (1, 2), 0, chunk=1)], [batch(0, 7, (1, 2), 0, chunk=1)]), [])


if __name__ == "__main__":
    unittest.main()
