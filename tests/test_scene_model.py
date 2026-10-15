"""Synthetic original-format models; no game assets or replacement graphics."""
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from scene_capture import ModelInput
from scene_model import Reader, camera_depth, camera_vertices, commands, decode_model, plane_states, select_lod


def put(page, at, value, size=2):
    for k in range(size):
        page[(at + k) & 65535] = (value >> (8 * k)) & 255


def words(values):
    return b"".join((v & 65535).to_bytes(2, "little") for v in values)


def fixture(shared=False, ordered=False):
    bank, data = bytearray(65536), bytearray(65536)
    put(bank, 0, 1)
    put(bank, 2, 0x100)
    bank[0x100:0x108] = words([5, 0x200, 32769, 0x300])
    bank[0x300:0x30A] = words([65535, 0, 0, 0, 0])
    bank[0x30A] = 255
    if shared:
        body = words([65535, 0, -2, 0]) + bytes([5, 7])
        body += words([1]) + bytes([5, 7, 0x41, 1, 0, 0x21, 2, 7, 255])
        for index, xyz in [(5, (-10, 20, 30)), (7, (40, -50, 60))]:
            for k, value in enumerate(xyz):
                put(data, 0x4EE6 + 16 * index + 4 * k, value, 4)
            put(data, 0x4EE6 + 16 * index + 12, 0xBAD0BAD0, 4)
    else:
        body = words([65535, 2]) + bytes([3, 1]) + words([10, 20, 30, 2, -1 if ordered else 1])
        body += words([0x8001, 0x8100]) + bytes([0x21, 255]) + words([1, 0, 0]) + bytes(4)
        body += bytes([2, 0, 1])
        if ordered:
            body += bytes([0, 255, 255])
        body += words([1, 2, 3, 0x8001, -2, 4, 8, 1]) + bytes([0, 1]) + words([0x500])
        bank[0x500:0x50F] = bytes([1, 0, 2, 0x41, 3, 0, 0x21, 4, 1, 0x81, 255, 1, 0, 255, 255])
    bank[0x200:0x200 + len(body)] = body
    request = [0] * 33
    request[0], request[9], request[13] = 1, 1, 12
    for k in (0, 4, 8):
        put(data, 0x7B28 + 2 * k, 32767)
    return ModelInput(10, request, data, bank, bytes(65536))


def prepared():
    data = bytearray(65536)
    put(data, 0x7D50, 0x1000)
    put(data, 0x7D4E, 0x9000)
    put(data, 0x9002, -1)
    for k in range(3):
        put(data, 0x1000 + 8 * k, 32767)
        put(data, 0x1024 + 8 * k, k + 1)
    for at, values in [(0x7CDE, (100, 200, 300)), (0x7CEA, (-10, -20, -30))]:
        for k, value in enumerate(values):
            put(data, at + 4 * k, value, 4)
    put(data, 0x7C72, -2)
    put(data, 0x7CA2, 16384)
    return data


class SceneModelTests(unittest.TestCase):
    def test_request_depth_and_original_detail_boundary(self):
        sample = fixture()
        self.assertEqual(camera_depth(sample), 5)
        self.assertEqual(select_lod(sample), 0x200)
        sample.request[13] = 14
        self.assertEqual(camera_depth(sample), 6)
        self.assertEqual(select_lod(sample), 0x300)
        sample.request[13] = (-14) & 65535
        self.assertEqual(camera_depth(sample), 7)
        self.assertEqual(select_lod(sample, 32768), 0x300)

    def test_complete_articulated_topology_and_materials(self):
        model = decode_model(fixture())
        self.assertEqual(model["rotations"], [(3, 1)])
        self.assertEqual(model["offsets"], [(10, 20, 30)])
        self.assertEqual([v[1:] for v in model["vertices"]], [(0, None, (1, 2, 3)), (1, None, (-2, 4, 8))])
        self.assertEqual(model["edges"], [(0, 1)])
        self.assertEqual(model["planes"][0][1:], (1, (1, 0, 0, 0), [(0x21, 255)]))
        self.assertEqual(model["lists"], [[0, 1]])
        material = model["materials"][0]
        self.assertEqual([c[0] for c in material], ["polygon", "lines", "dots", "condition", "polygon"])
        self.assertEqual([c[1] for c in material], [0x500, 0x503, 0x506, 0x509, 0x50B])
        self.assertEqual(material[-1][2:], (255, [0]))

    def test_zero_vertices_and_direct_empty_stream(self):
        model = decode_model(fixture(), 6)
        self.assertEqual((model["vertex_count"], model["plane_count"], model["part_count"]), (0, 0, 0))
        self.assertEqual(model["materials"], [[]])
        self.assertEqual(model["edges"], [])

    def test_shared_vertices_use_global_indices_and_padded_stride(self):
        model = decode_model(fixture(shared=True))
        self.assertEqual(model["vertex_count"], 65534)
        self.assertEqual([v[2:] for v in model["vertices"]], [(5, (-10, 20, 30)), (7, (40, -50, 60))])
        self.assertEqual(model["edges"], [(5, 7)])
        data = prepared()
        for k, value in enumerate((-5, -6, -7)):
            put(data, 0x7CD2 + 4 * k, value, 4)
        self.assertEqual(camera_vertices(model, data), {5: (-15, 14, 23), 7: (35, -56, 53)})

    def test_articulated_camera_geometry_keeps_hidden_vertices(self):
        model, data = decode_model(fixture()), prepared()
        self.assertEqual(camera_vertices(model, data), {0: (32867, 65734, 98601), 1: (-12, -12, -6)})
        self.assertEqual(plane_states(model, data), ([0], [1], set()))
        data[0x7D8E] = 21
        self.assertEqual(plane_states(model, data), ([255], [1], {0, 1}))
        put(data, 0x9002, 0)
        self.assertEqual(plane_states(model, data), ([127], [1], set()))

    def test_camera_sum_wraps_and_matrix_offsets_wrap(self):
        model = decode_model(fixture())
        model["part_count"] = 0
        model["vertices"] = [(0, 0, None, (1, 0, 0))]
        data = bytearray(65536)
        put(data, 0x7D50, 0xFFFF)
        put(data, 0xFFFF, 1)
        put(data, 0x7CD2, 0x7FFFFFFF, 4)
        self.assertEqual(camera_vertices(model, data), {0: (-2147483648, 0, 0)})

    def test_part_escape_also_applies_without_planes(self):
        sample = fixture()
        body = words([65535, 2]) + bytes([3, 1]) + words([10, 20, 30, 1, 0, 0x8001, 2, 3, 4, 0]) + bytes([255])
        sample.bank[0x200:0x200 + len(body)] = body
        model = decode_model(sample)
        self.assertEqual(model["vertices"][0][1:], (1, None, (2, 3, 4)))

    def test_depth_order_tree_is_retained_and_unknown_leaf_fails(self):
        sample = fixture(ordered=True)
        model = decode_model(sample)
        self.assertEqual(model["order"], [0, 255, 255])
        # Tree immediately precedes the vertex stream.
        sample.bank[(model["pointers"][0] - 2) & 65535] = 128
        with self.assertRaisesRegex(ValueError, "ordering-tree"):
            decode_model(sample)

    def test_missing_geometry_references_fail(self):
        for at, value in [(0x501, 1), (0x508, 2)]:
            sample = fixture()
            sample.bank[at] = value
            with self.assertRaisesRegex(ValueError, "missing geometry"):
                decode_model(sample)

    def test_logical_wrap_and_finite_malformed_stream_budget(self):
        page = bytearray(65536)
        page[65535], page[0] = 0x34, 0x12
        stream = Reader(page, 65535)
        self.assertEqual(stream.word(), 0x1234)
        self.assertEqual(stream.at, 1)
        with self.assertRaises(ValueError):
            stream.take(65536)
        with self.assertRaises(ValueError):
            commands(bytes([1]) * 65536, 0)
        sample = fixture()
        sample.bank[0x100:] = bytes(65536 - 0x100)
        with self.assertRaises(ValueError):
            select_lod(sample, 32768)

    def test_invalid_input_fails(self):
        sample = fixture()
        for depth in (-1, 32769):
            with self.assertRaises(ValueError):
                select_lod(sample, depth)
        sample.request[10] = 1
        with self.assertRaises(ValueError):
            decode_model(sample)
        with self.assertRaises(ValueError):
            Reader(bytes(4), 0)


if __name__ == "__main__":
    unittest.main()
