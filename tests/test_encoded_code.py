"""Adversarial validation of encoded code review without shipped game bytes."""
import copy
import hashlib
import json
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / "tools"), str(ROOT / "recompiler")]
from encoded_code import reviewed_bytes
from modules import Module


def sha(blob):
    return hashlib.sha256(blob).hexdigest()


class EncodedTests(unittest.TestCase):
    def setUp(self):
        self.folder = tempfile.TemporaryDirectory()
        self.addCleanup(self.folder.cleanup)
        self.root = Path(self.folder.name)
        self.raw = b"synthetic file identity"
        (self.root / "SETUP.EXE").write_bytes(self.raw)
        image = bytearray(0x2400)
        base = 0x1E10
        # MOV AX,1234 / RET, encoded with the original segment-relative IP.
        image[base + 0x50:base + 0x54] = bytes.fromhex("b83412c3")
        decoded = bytes(image)
        for ip in range(0x50, 0x564):
            image[base + ip] ^= ip & 255
        self.mod = Module("SETUP.EXE", 0, "exe", bytes(image), set())
        self.spec = dict(module="SETUP", encoding="xor-ip-low", segment="01E1", start="0050",
                         end_exclusive="0564", file_sha256=sha(self.raw), image_sha256=sha(image),
                         bootstrap_sha256=sha(image[base:base + 0x39]),
                         block_sha256=sha(image[base:base + 0x564]),
                         transform_raw_sha256=sha(image[base + 0x50:base + 0x564]),
                         decoded_block_sha256=sha(decoded[base:base + 0x564]), rows=[dict(
                             ip="0050", length=4, count=2, raw_sha256=sha(image[base + 0x50:base + 0x54]),
                             decoded_sha256=sha(decoded[base + 0x50:base + 0x54]),
                             purpose="synthetic return", review="independent synthetic review")])

    def read(self, spec=None, plain=None):
        path = self.root / "review.json"
        path.write_text(json.dumps(self.spec if spec is None else spec))
        return reviewed_bytes({"SETUP": self.mod}, self.root, path, plain)

    def test_exact_bytes_only_and_no_source_mutation(self):
        before = self.mod.image
        self.assertEqual(self.read(), {"SETUP": set(range(0x1E60, 0x1E64))})
        self.assertEqual(self.mod.image, before)
        self.assertEqual((self.root / "SETUP.EXE").read_bytes(), self.raw)
        self.assertEqual(sorted(p.name for p in self.root.iterdir()), ["SETUP.EXE", "review.json"])

    def test_every_source_and_phase_hash_is_enforced(self):
        for key in ("file_sha256", "image_sha256", "bootstrap_sha256", "block_sha256",
                    "transform_raw_sha256", "decoded_block_sha256"):
            spec = copy.deepcopy(self.spec)
            spec[key] = "0" * 64
            with self.subTest(key=key), self.assertRaisesRegex(ValueError, "hash differs"):
                self.read(spec)
        for key in ("raw_sha256", "decoded_sha256"):
            spec = copy.deepcopy(self.spec)
            spec["rows"][0][key] = "0" * 64
            with self.subTest(key=key), self.assertRaisesRegex(ValueError, "hash differs"):
                self.read(spec)

    def test_unsupported_identity_or_transform_fails(self):
        for key, value in (("module", "VGAME"), ("encoding", "xor-index-low"),
                           ("segment", "0000"), ("start", "0051"), ("end_exclusive", "0565")):
            spec = copy.deepcopy(self.spec)
            spec[key] = value
            with self.subTest(key=key), self.assertRaises(ValueError):
                self.read(spec)

    def test_overlaps_relocations_counts_and_missing_reviews_fail(self):
        with self.assertRaisesRegex(ValueError, "overlapping"):
            self.read(plain={"SETUP": {0x1E61}})
        spec = copy.deepcopy(self.spec)
        spec["rows"] *= 2
        with self.assertRaisesRegex(ValueError, "overlapping"):
            self.read(spec)
        for key, value in (("count", 3), ("purpose", " "), ("review", " "),
                           ("ip", "004F"), ("length", 0), ("length", 0x600)):
            spec = copy.deepcopy(self.spec)
            spec["rows"][0][key] = value
            with self.subTest(key=key), self.assertRaises(ValueError):
                self.read(spec)
        self.mod.relocs.add(0x1E49)
        with self.assertRaisesRegex(ValueError, "relocation"):
            self.read()

    def test_partial_instruction_with_matching_hashes_fails(self):
        row = self.spec["rows"][0]
        row.update(length=2, count=1, raw_sha256=sha(self.mod.image[0x1E60:0x1E62]),
                   decoded_sha256=sha(bytes.fromhex("b834")))
        with self.assertRaisesRegex(ValueError, "partial instruction"):
            self.read()


if __name__ == "__main__":
    unittest.main()
