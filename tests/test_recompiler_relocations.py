import re
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace

import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "recompiler"))
from relocations import relocation_metadata, write_relocation_metadata


def module(name="T.EXE", image=b"\x00" * 8, relocs=(), file_hash=1, origin=0):
    return SimpleNamespace(name=name, image=image, relocs=relocs, file_hash=file_hash, origin=origin)


def numbers(text, name):
    body = re.search(r"\b" + name + r"\[\] = \{([^}]*)\}", text, re.S)[1]
    return [int(x.strip().removesuffix("ull"), 0) for x in body.split(",") if x.strip()]


class Metadata(unittest.TestCase):
    def test_module_order_empty_com_and_word_at_image_end(self):
        # Expected offsets are image-relative, including for origin 0100h.
        mods = [module("Z.EXE", relocs=[6, 1], file_hash=0xFEDCBA9876543210),
                module("A.COM", origin=0x100), module("S.LOG", relocs=[0])]
        text = relocation_metadata(mods)
        self.assertEqual([0, 2, 2, 3], numbers(text, "RC_RELOC_STARTS"))
        self.assertEqual([1, 6, 0], numbers(text, "RC_RELOC_OFFSETS"))
        self.assertEqual([0xFEDCBA9876543210, 1, 1], numbers(text, "RC_RELOC_HASHES"))
        self.assertLess(text.index('"Z.EXE"'), text.index('"A.COM"'))
        self.assertLess(text.index('"A.COM"'), text.index('"S.LOG"'))

    def test_bad_sites_never_replace_an_existing_output(self):
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp, "gen_relocs.c")
            for sites in [[-1], [7], [8], [1, 2], [1, 1], [True], [1.5]]:
                with self.subTest(sites=sites):
                    out.write_text("previous valid metadata\n")
                    with self.assertRaises(ValueError):
                        write_relocation_metadata([module("OK.EXE"), module(relocs=sites)], tmp)
                    self.assertEqual("previous valid metadata\n", out.read_text())

    def test_duplicate_identity_and_invalid_hash_rejected(self):
        for mods in [[module(), module()], [module(file_hash=-1)], [module(file_hash=2**64)]]:
            with self.subTest(mods=mods), self.assertRaises(ValueError):
                relocation_metadata(mods)

    def test_empty_fixup_set_has_a_valid_c_sentinel(self):
        text = relocation_metadata([module("L.COM", origin=0x100)])
        self.assertIn("RC_NRELOCS = 0;", text)
        self.assertEqual([0, 0], numbers(text, "RC_RELOC_STARTS"))
        self.assertEqual([0], numbers(text, "RC_RELOC_OFFSETS"))


if __name__ == "__main__":
    unittest.main()
