"""Decode original model topology before visibility filtering.

Consumes private scene_capture.ModelInput snapshots. This research decoder
does not render, identify persistent world instances or publish original
assets. The selected level retains original vertex/plane/edge indices and
command addresses, including hidden geometry. Integer pose preparation and
rendered material parity are separate requirements.
"""
from __future__ import annotations

from scene_capture import ModelInput, word


def signed(value: int, bits: int = 16) -> int:
    value &= (1 << bits) - 1
    return value - (1 << bits) if value & (1 << (bits - 1)) else value


class Reader:
    """Real-mode logical offsets wrap; a malformed stream has a finite budget."""

    def __init__(self, data: bytes, offset: int):
        if len(data) != 65536:
            raise ValueError("model bank must contain one complete segment")
        self.data = data
        self.at = offset & 65535
        self.used = 0

    def take(self, count: int) -> bytes:
        if count < 0 or self.used + count > 65536:
            raise ValueError("model stream exceeds one segment")
        result = bytes(self.data[(self.at + k) & 65535] for k in range(count))
        self.at = (self.at + count) & 65535
        self.used += count
        return result

    def byte(self) -> int:
        return self.take(1)[0]

    def word(self) -> int:
        return int.from_bytes(self.take(2), "little")


def camera_depth(sample: ModelInput) -> int:
    """120A:00CF selects detail by |high_word(position * camera).z|.

    Products and sums wrap at 32 bits, as 1452:0330 does. This is the
    original selection distance, not Euclidean distance or projected size.
    """
    position = [signed(v) for v in sample.request[11:14]]
    total = sum(position[row] * signed(word(sample.data, 0x7B28 + 2 * (3 * row + 2)))
                for row in range(3))
    return abs(signed((total & 0xFFFFFFFF) >> 16))


def select_lod(sample: ModelInput, depth: int | None = None) -> int:
    index = sample.model_index
    if not 0 <= index < word(sample.bank, 0):
        raise ValueError("model index outside original bank")
    depth = camera_depth(sample) if depth is None else depth
    if not 0 <= depth <= 32768:
        raise ValueError("detail distance outside original signed-word magnitude")
    table = Reader(sample.bank, word(sample.bank, 2 + 2 * index))
    while True:
        threshold, offset = table.word(), table.word()
        if depth <= threshold:
            return offset


def commands(bank: bytes, offset: int) -> list[tuple]:
    """120A:0CE9: preserve conditional commands and their stream addresses.

    Polygon commands carry edges then a colour-table index; line/dot
    commands carry the colour index first. FF ends a stream, and an FF
    polygon colour suppresses that polygon. A condition is not evaluated
    during decoding: its original setting can change between frames.
    """
    stream = Reader(bank, offset)
    result = []
    while True:
        start, opcode = stream.at, stream.byte()
        if opcode == 255:
            return result
        if opcode & 128:
            result.append(("condition", start, opcode & 127, signed(stream.byte(), 8)))
            continue
        count = opcode & 31
        if not count:
            raise ValueError("zero-count material command requires an original loop underflow")
        if opcode & 32:
            colour, items, kind = stream.byte(), list(stream.take(count)), "dots"
        elif opcode & 64:
            colour, items, kind = stream.byte(), list(stream.take(count)), "lines"
        else:
            items, colour, kind = list(stream.take(count)), stream.byte(), "polygon"
        result.append((kind, start, colour, items))


def decode_model(sample: ModelInput, depth: int | None = None) -> dict:
    bank = sample.bank
    offset = select_lod(sample, depth)
    stream = Reader(bank, offset)
    own_range, parts = stream.word(), stream.word()
    prefix = [stream.at]
    rotations, offsets = [], []
    if signed(parts) > 0:
        rotations = [tuple(stream.take(2)) for _ in range(parts - 1)]
        offsets = [tuple(signed(stream.word()) for _ in range(3)) for _ in range(parts - 1)]
    vertex_count = signed(stream.word())
    prefix.append(stream.at)
    plane_count = signed(stream.word())
    planes, part, conditions = [], 0, []
    while len(planes) < abs(plane_count):
        start, a = stream.at, stream.word()
        if a & 0xFF00 == 0x8000:
            part = a & 255
            continue
        if a == 0x8100:
            conditions.append(tuple(stream.take(2)))
            continue
        b, c = stream.word(), stream.word()
        d = int.from_bytes(stream.take(4), "little", signed=True)
        planes.append((start, part, (signed(a), signed(b), signed(c), d), conditions))
        conditions = []
    lists = [list(stream.take(stream.byte())) for _ in range(abs(plane_count))]
    order = list(stream.take(2 * abs(plane_count) + 1)) if plane_count < 0 else []
    prefix.append(stream.at)
    pointers, vertices, part = [stream.at], [], 0
    while len(vertices) < abs(vertex_count):
        start = stream.at
        if vertex_count < 0:
            shared = stream.byte()
            # The cache uses a 16-byte stride, with three signed dwords;
            # its fourth dword is padding, not a packed 12-byte stride.
            coords = tuple(int.from_bytes(bytes(sample.data[(0x4EE6 + 16 * shared + 4 * k + b) & 65535]
                                                for b in range(4)), "little", signed=True) for k in range(3))
            vertices.append((start, part, shared, coords))
            continue
        x = stream.word()
        # 0815 itself recognizes escapes even with no plane list.
        if 0x8000 <= x < 0x8010:
            part = x & 15
            continue
        vertices.append((start, part, None, (signed(x), signed(stream.word()), signed(stream.word()))))
    pointers.append(stream.at)
    edges = [tuple(stream.take(2)) for _ in range(stream.word())]
    pointers.append(stream.at)
    faces = [stream.word() for _ in range(abs(plane_count))] if plane_count else [stream.at]
    material = [commands(bank, at) for at in faces]
    vertex_ids = set(range(vertex_count)) if vertex_count >= 0 else {v[2] for v in vertices}
    if any(a not in vertex_ids or b not in vertex_ids for a, b in edges):
        raise ValueError("edge outside decoded vertex set")
    if any(i not in vertex_ids for indices in lists for i in indices):
        raise ValueError("plane list outside decoded vertex set")
    for face in material:
        for kind, at, *payload in face:
            if kind == "condition":
                continue
            _, items = payload
            valid = vertex_ids if kind == "dots" else range(len(edges))
            if any(i not in valid for i in items):
                raise ValueError(f"{kind} at {at:04x} references missing geometry")
    # The captured banks use ordinary plane nodes/FF children. Preserve
    # the full tree, but fail explicitly on an unproven extended face leaf.
    if order and (order[0] >= abs(plane_count) or any(i != 255 and i >= abs(plane_count) for i in order[1:])):
        raise ValueError("extended ordering-tree leaf needs original-execution proof")
    return dict(lod=offset, own_range=own_range, part_count=parts,
                rotations=rotations, offsets=offsets, vertex_count=vertex_count & 65535,
                plane_count=plane_count & 65535, planes=planes, lists=lists, order=order,
                vertices=vertices, edges=edges, materials=material, prefix=prefix, pointers=pointers)


def long(data: bytes, offset: int) -> int:
    return signed(word(data, offset) | (word(data, offset + 2) << 16), 32)


def camera_vertices(model: dict, prepared: bytes) -> dict[int, tuple]:
    """Complete geometry through original prepared poses, without culling.

    `prepared` is DS after original part preparation, not the request's
    earlier DS. Preparing those poses independently remains separate work.
    """
    base = word(prepared, 0x7D50)
    result = {}
    for index, (_, part, shared, coords) in enumerate(model["vertices"]):
        if shared is not None:
            result[shared] = tuple(signed(coords[k] + long(prepared, 0x7CD2 + 4 * k), 32) for k in range(3))
            continue
        matrix = base + 36 * part
        eye = 0x7CDE + 12 * part if signed(model["part_count"]) > 0 else 0x7CD2
        result[index] = tuple(signed(sum(coords[row] * signed(word(prepared, matrix + 2 * (3 * row + k)))
                                         for row in range(3)) + long(prepared, eye + 4 * k), 32) for k in range(3))
    return result


def plane_states(model: dict, prepared: bytes) -> tuple[list, list, set]:
    """Original fixed-point face tests, conditional settings and light shades."""
    visibility, shades, vertices = [], [], set()
    settings = word(prepared, 0x7D4E)
    for index, (_, part, (a, b, c, d), conditions) in enumerate(model["planes"]):
        enabled = all(signed(word(prepared, settings + 2 * (slot & 31))) == signed(value, 8)
                      for slot, value in conditions)
        normal = (a, b, c)
        eye = [signed(word(prepared, 0x7C6C + 6 * part + 2 * k)) for k in range(3)]
        facing = signed(sum(normal[k] * eye[k] for k in range(3)) + d, 32) >= 0
        flag = (255 if facing or prepared[0x7D8E] else 0) & (255 if enabled else 127)
        visibility.append(flag)
        light = [signed(word(prepared, 0x7C9C + 6 * part + 2 * k)) for k in range(3)]
        shades.append(((sum(normal[k] * light[k] for k in range(3)) << 2) & 0xFFFFFFFF) >> 16 & 255)
        if flag > 127:
            vertices.update(model["lists"][index])
    return visibility, shades, vertices
