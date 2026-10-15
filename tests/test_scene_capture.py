"""Capture completeness is a prerequisite for the scene decoder."""
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from scene_capture import audit, inputs, word


def put(data, at, value):
    data[at & 65535] = value & 255
    data[(at + 1) & 65535] = value >> 8


def fixture(clock=10):
    request = [1, 0xA000, 0x9000, 0xFFFC, 0x1234, 0xCF, 0x1111, 0x2222,
               0xFFFF, 3, 1, 0xFFFE, 0x7FFF, 0x8000, 0xABCD, 0xFFFC,
               0, 0, 0, 0, 0, 319, 90, 0x5678] + [0] * 8 + [0xF202]
    request[28] = request[3]
    ds = bytearray(65536)
    bank = bytearray((k * 37) & 255 for k in range(65536))
    stack = bytearray(65536)
    put(ds, 0x8590, request[8])
    put(ds, 0x85FE, 319)
    put(ds, 0x8600, 90)
    put(bank, 0, 3)
    put(bank, 4, 0x1234)
    for k, field in enumerate([6, 7, 10, 11, 12, 13, 14, 15]):
        put(stack, request[3] + 2 * k, request[field])
    lines = ["i " + str(clock) + " " + " ".join(map(str, request))]
    for role, page in enumerate([ds, bank, stack]):
        start, size = (request[3], 256) if role == 2 else (0, 65536)
        for at in range(0, size, 512):
            count = min(512, size - at)
            offset = (start + at) & 65535
            values = [1, role, request[(1, 8, 2)[role]], offset, count]
            values += [word(page, offset + k) for k in range(0, count, 2)]
            lines.append("j " + str(clock) + " " + " ".join(map(str, values)))
    return lines, bytes(bank)


class SceneCaptureTests(unittest.TestCase):
    def setUp(self):
        self.folder = tempfile.TemporaryDirectory()
        self.addCleanup(self.folder.cleanup)
        self.path = Path(self.folder.name) / "capture.log"
        self.lines, self.bank = fixture()

    def write(self, lines):
        self.path.write_text("\n".join(lines) + "\n")

    def rejected(self, lines):
        self.write(lines)
        with self.assertRaises(ValueError):
            audit(self.path)

    def test_complete_wrapped_stack_and_original_bank(self):
        self.write(self.lines)
        models = list(inputs(self.path))
        self.assertEqual(len(models), 1)
        self.assertEqual(models[0].bank, self.bank)
        self.assertEqual(word(models[0].stack, 0), 1)
        result = audit(self.path)
        self.assertEqual(result["complete_input_bytes"], 131328)
        self.assertEqual(result["models"][0]["model_table_offset"], 0x1234)
        self.assertFalse(result["decoded_scene"])

    def test_two_requests_keep_their_memory(self):
        other, _ = fixture(11)
        self.write(self.lines + other)
        self.assertEqual(audit(self.path)["requests"], 2)

    def test_missing_memory_and_duplicate_chunks(self):
        self.rejected(self.lines[:-1])
        self.rejected(self.lines + [self.lines[1]])

    def test_wrong_segment_clock_and_memory_word(self):
        for field, value in [(1, "11"), (4, "0"), (7, "65536")]:
            lines = self.lines.copy()
            row = lines[1].split()
            row[field] = value
            lines[1] = " ".join(row)
            self.rejected(lines)

    def test_metadata_must_match_original_memory(self):
        for index in (9, 10, 16, 21, 28):
            lines = self.lines.copy()
            row = lines[0].split()
            row[2 + index] = str(int(row[2 + index]) ^ 1)
            lines[0] = " ".join(row)
            self.rejected(lines)

    def test_empty_capture_and_memory_without_request(self):
        self.rejected([])
        self.rejected(self.lines[1:])

    def test_repeated_clock_is_not_a_second_instance(self):
        self.rejected(self.lines + self.lines)


if __name__ == "__main__":
    unittest.main()
