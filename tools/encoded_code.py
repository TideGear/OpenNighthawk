"""Strict review validation for SETUP's media-check XOR representation.

Only hashes, instruction counts and address sets leave this function. Decoded
bytes stay in memory; the installed file and the module image are never edited.
This establishes workload, not reachability or matched credit.
"""
import hashlib
import json
from pathlib import Path


def reviewed_bytes(mods, data_dir, manifest_path, plain=None):
    from x86dec import DecodeError, K_INVALID, decode

    def require(condition, message):
        if not condition:
            raise ValueError("encoded code review: " + message)

    def digest(blob):
        return hashlib.sha256(blob).hexdigest()

    spec = json.loads(Path(manifest_path).read_text(encoding="utf-8"))
    require(spec["module"] == "SETUP" and spec["encoding"] == "xor-ip-low",
            "unsupported module or transform")
    seg, start, end = (int(spec[k], 16) for k in ("segment", "start", "end_exclusive"))
    require((seg, start, end) == (0x01E1, 0x0050, 0x0564), "unsupported transform bounds")
    mod = mods.get("SETUP")
    require(mod is not None, "missing SETUP module")
    raw = (Path(data_dir) / "SETUP.EXE").read_bytes()
    require(digest(raw) == spec["file_sha256"], "raw file hash differs")
    require(digest(mod.image) == spec["image_sha256"], "image hash differs")
    base, stop = mod.off(seg, 0), mod.off(seg, end)
    require(0 <= base < stop <= len(mod.image), "transform outside image")
    require(not (set(range(base, stop)) & mod.reloc_bytes()), "relocation inside transform block")
    require(digest(mod.image[base:base + 0x39]) == spec["bootstrap_sha256"], "bootstrap hash differs")
    require(digest(mod.image[base:stop]) == spec["block_sha256"], "source block hash differs")
    require(digest(mod.image[base + start:stop]) == spec["transform_raw_sha256"], "transform hash differs")
    decoded = bytearray(mod.image)
    for ip in range(start, end):
        decoded[base + ip] ^= ip & 0xFF
    require(digest(decoded[base:stop]) == spec["decoded_block_sha256"], "decoded block hash differs")
    out = set()
    require(bool(spec["rows"]), "empty code review")
    for row in spec["rows"]:
        ip, size, count = int(row["ip"], 16), row["length"], row["count"]
        require(type(size) is int and type(count) is int and
                start <= ip < ip + size <= end and count > 0, "invalid code range")
        require(row["purpose"].strip() and row["review"].strip(), "missing purpose or second review")
        require(digest(mod.image[base + ip:base + ip + size]) == row["raw_sha256"], "row source hash differs")
        require(digest(decoded[base + ip:base + ip + size]) == row["decoded_sha256"], "row decoded hash differs")
        span = set(range(seg * 16 + ip, seg * 16 + ip + size))
        require(not (span & (out | set((plain or {}).get("SETUP", ())))), "overlapping code reviews")
        pos, found = ip, 0
        try:
            while pos < ip + size:
                ins = decode(decoded, base + pos, pos)
                require(ins.kind != K_INVALID and 0 < ins.length <= ip + size - pos,
                        "invalid or partial instruction")
                pos += ins.length
                found += 1
        except DecodeError as error:
            raise ValueError("encoded code review: undecodable instruction") from error
        require(found == count, "instruction count differs")
        out.update(span)
    return {"SETUP": out}
