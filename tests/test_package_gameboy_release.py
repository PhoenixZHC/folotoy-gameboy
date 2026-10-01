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
from pack_roms import pack, inspect_image  # noqa: E402


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

    def test_preloaded_game_is_in_normal_rom_partition_and_verified(self) -> None:
        with tempfile.TemporaryDirectory() as directory, patch.object(release, "verify_firmware_layout"):
            rom = Path(directory) / "local.gb"
            rom.write_bytes(bytes(32768))
            packed = pack([rom], ["口袋妖怪红"])
            image = release.expected_image(self.offsets, self.images, packed)
            self.assertEqual(len(image), 8 * 1024 * 1024)
            self.assertEqual(inspect_image(image[0x310000:0x710000]), ["口袋妖怪红"])
            self.assertEqual(image[0x311000:0x319000], rom.read_bytes())
            self.assertEqual(image[0x9000:0x10000], b"\xff" * 0x7000)
            self.assertEqual(image[0x70E000:0x710000], b"\xff" * 8192)
            self.assertEqual(image[0x710000:], self.images["saves.bin"])
            release.validate(image, Path(directory), self.offsets, self.images, packed)
            # Clean verification must never silently accept the preloaded variant.
            with self.assertRaisesRegex(ValueError, "unexpected bytes"):
                release.validate(image, Path(directory), self.offsets, self.images)
            for address in (0x311234, 0x9000, 0x70E000, 0x710000):
                corrupt = bytearray(image)
                corrupt[address] ^= 1
                with self.subTest(address=address), self.assertRaisesRegex(ValueError, "unexpected bytes"):
                    release.validate(bytes(corrupt), Path(directory), self.offsets, self.images, packed)
            with self.assertRaisesRegex(ValueError, "directory sectors"):
                release.expected_image(self.offsets, self.images,
                                       packed.ljust(0x400000, b"\xff"))

    def test_cli_preloaded_roundtrip_and_name_requires_rom(self) -> None:
        with tempfile.TemporaryDirectory() as directory, patch.object(release, "verify_firmware_layout"), \
                patch.object(release, "inputs", return_value=(self.offsets, self.images)):
            rom = Path(directory) / "source-with-a-name-too-long-for-the-game-list.gb"
            rom.write_bytes(bytes(32768))
            second_rom = Path(directory) / "second.gb"
            second_data = bytearray(32768)
            second_data[0x150] = 1
            second_rom.write_bytes(second_data)
            output = Path(directory) / "preloaded.bin"
            args = ["package_gameboy_release.py", "--rom", str(rom), "--rom-name", "口袋妖怪红",
                    "--rom", str(second_rom), "--rom-name", "超级马里奥大陆",
                    "--output", str(output)]
            with patch.object(sys, "argv", args):
                self.assertEqual(release.main(), 0)
            self.assertEqual(inspect_image(output.read_bytes()[0x310000:0x710000]),
                             ["口袋妖怪红", "超级马里奥大陆"])
            with patch.object(sys, "argv", args + ["--verify"]):
                self.assertEqual(release.main(), 0)
            with patch.object(sys, "argv", ["package_gameboy_release.py", "--rom-name", "orphan"]):
                with self.assertRaises(SystemExit) as error:
                    release.main()
                self.assertEqual(error.exception.code, 2)
            with patch.object(sys, "argv", args + ["--rom", str(second_rom)]):
                with self.assertRaises(SystemExit) as error:
                    release.main()
                self.assertEqual(error.exception.code, 2)


if __name__ == "__main__":
    unittest.main()
