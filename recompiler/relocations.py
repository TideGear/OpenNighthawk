"""Emit test-only loader fixups without changing the production module ABI."""
import json
from pathlib import Path


def relocation_metadata(mods):
    """Validate all input before producing C; offsets are relative to the image."""
    names, hashes, starts, offsets, keys = [], [], [0], [], set()
    for mod in mods:
        key = (mod.name, mod.file_hash)
        if key in keys:
            raise ValueError("duplicate relocation module: %s" % mod.name)
        if not isinstance(mod.file_hash, int) or not 0 <= mod.file_hash < 2**64:
            raise ValueError("invalid relocation module hash: %s" % mod.name)
        keys.add(key)
        sites = list(mod.relocs)
        if any(type(off) is not int or off < 0 or off + 1 >= len(mod.image) for off in sites):
            raise ValueError("relocation outside image: %s" % mod.name)
        sites.sort()
        if any(b <= a + 1 for a, b in zip(sites, sites[1:])):
            raise ValueError("overlapping relocation words: %s" % mod.name)
        names.append(json.dumps(mod.name))
        hashes.append("0x%016Xull" % mod.file_hash)
        offsets.extend(sites)
        starts.append(len(offsets))
    def array(kind, name, values):
        values = values or ["0"]
        lines = ["  " + ", ".join(values[i:i + 8]) + "," for i in range(0, len(values), 8)]
        return "const %s %s[] = {\n%s\n};\n" % (kind, name, "\n".join(lines))
    text = '/* GENERATED - test loader relocation metadata. */\n#include <stdint.h>\n\n'
    text += array("char *const", "RC_RELOC_NAMES", names)
    text += array("uint64_t", "RC_RELOC_HASHES", hashes)
    text += array("uint32_t", "RC_RELOC_STARTS", [str(n) for n in starts])
    text += array("uint32_t", "RC_RELOC_OFFSETS", [str(n) for n in offsets])
    text += "const unsigned RC_NRELOC_MODULES = %d;\n" % len(names)
    text += "const unsigned RC_NRELOCS = %d;\n" % len(offsets)
    return text


def write_relocation_metadata(mods, out_dir):
    text = relocation_metadata(mods)
    Path(out_dir, "gen_relocs.c").write_text(text, encoding="utf-8", newline="\n")
