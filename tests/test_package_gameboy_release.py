#!/usr/bin/env python3
"""Ensure the public image cannot silently carry ROMs or private Flash data."""

from __future__ import annotations

import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import package_gameboy_release as release  # noqa: E402


class PublicReleaseImageTests(unittest.TestCase):
    def setUp(self) -> None:
        self.offsets = {
            "bootloader/bootloader.bin": 0,
            "partition_table/partition-table.bin": 0x8000,
            "FoloToy-AI-Passport.bin": 0x10000,
        }
        self.images = {
            "bootloader/bootloader.bin": b"BOOT",
            "partition_table/partition-table.bin": b"TABLE",
            "FoloToy-AI-Passport.bin": b"\xe9APP",
            "saves.bin": b"INITIAL" + b"\xff" * (0xF0000 - 7),
        }

    def test_exact_size_and_private_partitions_are_erased(self) -> None:
        image = release.expected_image(self.offsets, self.images)
        self.assertEqual(len(image), 8 * 1024 * 1024)
        for start, end in ((0x9000, 0x10000), (0x310000, 0x710000)):
            self.assertEqual(image[start:end], b"\xff" * (end - start))
        self.assertEqual(image[0x710000:0x800000], self.images["saves.bin"])

    def test_rejects_rom_and_nvs_data_and_wrong_size(self) -> None:
        image = release.expected_image(self.offsets, self.images)
        with tempfile.TemporaryDirectory() as directory, patch.object(release, "verify_firmware_layout"):
            build_dir = Path(directory)
            (build_dir / "FoloToy-AI-Passport.bin").write_bytes(self.images["FoloToy-AI-Passport.bin"])
            release.validate(image, build_dir, self.offsets, self.images)
            for address in (0x9000, 0x310000, 0x710000):
                modified = bytearray(image)
                modified[address] ^= 1
                with self.subTest(address=address), self.assertRaisesRegex(ValueError, "unexpected bytes"):
                    release.validate(bytes(modified), build_dir, self.offsets, self.images)
            with self.assertRaisesRegex(ValueError, "exactly"):
                release.validate(image[:-1], build_dir, self.offsets, self.images)


if __name__ == "__main__":
    unittest.main()
