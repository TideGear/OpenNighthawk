"""Audit opt-in pre-visibility model snapshots; original bytes stay private.

F117R_OBSERVE_SCENE=N with --observe captures at most N requests in that
clock window. Keep i/j records. This verifies complete inputs, not a decoded
scene or stable instance identity. Run: py tools/scene_capture.py LOG --out JSON
"""
from __future__ import annotations

import argparse
from dataclasses import dataclass
import hashlib
import json
from pathlib import Path
from typing import Iterator

VERSION = 1
REQUEST_N = 33
STACK_BYTES = 256
CHUNK_BYTES = 512


@dataclass
class ModelInput:
    clock: int
    request: list[int]
    data: bytes
    bank: bytes
    stack: bytes  # logical SS segment; only SP..SP+255 is captured

    @property
    def model_index(self) -> int:
        return self.request[10]


def word(data: bytes | bytearray, at: int) -> int:
    return data[at & 65535] | data[(at + 1) & 65535] << 8


def inputs(path: Path) -> Iterator[ModelInput]:
    current = None
    pages: list[bytearray] = []
    covered: list[bytearray] = []
    totals = [0, 0, 0]
    previous_clock = -1

    def finish() -> ModelInput:
        clock, request = current
        if totals != [65536, 65536, STACK_BYTES]:
            raise ValueError(f"{clock}: incomplete input memory: {totals}")
        if word(pages[0], 0x8590) != request[8] or word(pages[1], 0) != request[9]:
            raise ValueError(f"{clock}: model-bank metadata differs from memory")
        sp = request[3]
        for k, field in enumerate([6, 7, 10, 11, 12, 13, 14, 15]):
            if word(pages[2], sp + 2 * k) != request[field]:
                raise ValueError(f"{clock}: stack argument {k} differs from metadata")
        if pages[0][0x7D8E] != request[16]:
            raise ValueError(f"{clock}: projection mode differs from memory")
        for at, field in [(0x8602, 17), (0x8604, 18), (0x85FA, 19),
                          (0x85FC, 20), (0x85FE, 21), (0x8600, 22)]:
            if word(pages[0], at) != request[field]:
                raise ValueError(f"{clock}: view metadata differs from memory")
        if request[24 + 4] != sp:  # R_SP, as in cpu.h
            raise ValueError(f"{clock}: stack pointer differs from register snapshot")
        return ModelInput(clock, request, bytes(pages[0]), bytes(pages[1]), bytes(pages[2]))

    with path.open(encoding="utf-8") as stream:
        for line_no, line in enumerate(stream, 1):
            fields = line.split()
            if not fields or fields[0] not in ("i", "j"):
                continue
            try:
                clock = int(fields[1])
                values = [int(x) for x in fields[2:]]
            except (ValueError, IndexError) as exc:
                raise ValueError(f"line {line_no}: malformed scene record") from exc
            if fields[0] == "i":
                if current is not None:
                    yield finish()
                if len(values) != REQUEST_N or values[0] != VERSION or clock <= previous_clock:
                    raise ValueError(f"line {line_no}: invalid request header/clock")
                if any(x < 0 or x > 65535 for x in values[1:]):
                    raise ValueError(f"line {line_no}: request word outside 16 bits")
                current = clock, values
                previous_clock = clock
                pages = [bytearray(65536) for _ in range(3)]
                covered = [bytearray(65536) for _ in range(3)]
                totals = [0, 0, 0]
                continue
            if current is None or clock != current[0] or len(values) < 5:
                raise ValueError(f"line {line_no}: memory without its request")
            version, role, segment, offset, size = values[:5]
            if (version != VERSION or role not in (0, 1, 2) or not 0 <= offset <= 65535
                    or not 0 < size <= CHUNK_BYTES or size % 2 or len(values) != 5 + size // 2):
                raise ValueError(f"line {line_no}: invalid memory chunk")
            request = current[1]
            if segment != request[(1, 8, 2)[role]]:
                raise ValueError(f"line {line_no}: memory segment differs from request")
            if any(x < 0 or x > 65535 for x in values[5:]):
                raise ValueError(f"line {line_no}: memory word outside 16 bits")
            for k in range(size):
                at = (offset + k) & 65535
                if covered[role][at] or (role == 2 and ((at - request[3]) & 65535) >= STACK_BYTES):
                    raise ValueError(f"line {line_no}: repeated/out-of-range memory byte")
                covered[role][at] = 1
                pages[role][at] = (values[5 + k // 2] >> (8 * (k % 2))) & 255
            totals[role] += size
    if current is not None:
        yield finish()


def audit(path: Path) -> dict:
    rows = []
    for model in inputs(path):
        index = model.model_index
        rows.append({"clock": model.clock, "model": index, "bank_count": model.request[9],
                     "valid_model": index < model.request[9],
                     "model_table_offset": word(model.bank, 2 + 2 * index) if index < model.request[9] else None,
                     "caller": model.request[6:8], "orthographic_shift": model.request[16],
                     "data_sha256": hashlib.sha256(model.data).hexdigest(),
                     "bank_sha256": hashlib.sha256(model.bank).hexdigest()})
    if not rows:
        raise ValueError("no complete model inputs; an empty capture is not proof")
    return {"requests": len(rows), "complete_input_bytes": len(rows) * (2 * 65536 + STACK_BYTES),
            "models": rows, "decoded_scene": False, "stable_instance_identity": False, "errors": []}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log", type=Path)
    parser.add_argument("--out", required=True, type=Path)
    args = parser.parse_args()
    result = audit(args.log)
    args.out.write_text(json.dumps(result, indent=2) + "\n")
    print(f"{result['requests']} complete model inputs; {result['complete_input_bytes']} original input bytes")


if __name__ == "__main__":
    main()
