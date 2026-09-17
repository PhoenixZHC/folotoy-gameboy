import tempfile
import unittest
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from pack_roms import inspect_image, pack


def dummy_rom() -> bytes:
    data = bytearray(32768)
    data[0x147] = 0x00
    data[0x148] = 0x00
    data[0x149] = 0x00
    return bytes(data)


class PackTests(unittest.TestCase):
    def test_round_trip_and_hash(self):
        with tempfile.TemporaryDirectory() as temp:
            rom = Path(temp) / "PUBLIC_TEST.gb"
            rom.write_bytes(dummy_rom())
            image = pack([rom])
            self.assertEqual(inspect_image(image), ["PUBLIC_TEST"])
            modified = bytearray(image)
            modified[4096] ^= 1
            with self.assertRaisesRegex(ValueError, "hash mismatch"):
                inspect_image(modified)

    def test_rejects_unsupported_mapper(self):
        with tempfile.TemporaryDirectory() as temp:
            rom = Path(temp) / "BAD.gb"
            data = bytearray(dummy_rom())
            data[0x147] = 0xff
            rom.write_bytes(data)
            with self.assertRaisesRegex(ValueError, "Unsupported"):
                pack([rom])

    def test_chinese_filename_round_trip(self):
        with tempfile.TemporaryDirectory() as temp:
            rom = Path(temp) / "口袋妖怪蓝.gb"
            rom.write_bytes(dummy_rom())
            self.assertEqual(inspect_image(pack([rom])), ["口袋妖怪蓝"])

    def test_truncated_image_rejected(self):
        with tempfile.TemporaryDirectory() as temp:
            rom = Path(temp) / "TEST.gb"
            rom.write_bytes(dummy_rom())
            with self.assertRaisesRegex(ValueError, "outside image"):
                inspect_image(pack([rom])[:-1])


if __name__ == "__main__":
    unittest.main()
