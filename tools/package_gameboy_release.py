#!/usr/bin/env python3
"""Build or verify an 8 MiB Game Boy image, optionally with local preinstalled ROMs."""

from __future__ import annotations

import argparse
import hashlib
import os
import subprocess
import sys
import tempfile
from pathlib import Path

from pack_roms import inspect_image, pack
from verify_firmware import (
    FLASH_SIZE,
    REQUIRED_IMAGES,
    parse_flash_args,
    parse_partition_table,
    verify_firmware_layout,
)


EXPECTED_PARTITIONS = (
    ("nvs", 1, 2, 0x9000, 0x6000),
    ("phy_init", 1, 1, 0xF000, 0x1000),
    ("game_app", 0, 0, 0x10000, 0x300000),
    ("roms", 1, 0x40, 0x310000, 0x400000),
    ("saves", 1, 0x82, 0x710000, 0xF0000),
)
DEFAULT_OUTPUT = Path("artifacts/releases/FoloToy-GameBoy-8MB-clean.bin")
PRELOADED_OUTPUT = Path("artifacts/releases/FoloToy-GameBoy-8MB-preloaded.bin")
ROM_OFFSET = 0x310000
ROM_DATA_SIZE = 0x400000 - 2 * 4096


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def clean_saves_image(project_root: Path) -> bytes:
    source = project_root / "assets/initial_saves"
    contents = sorted(path.relative_to(source).as_posix() for path in source.rglob("*") if path.is_file())
    if contents != ["README.txt"]:
        raise ValueError(f"initial saves must contain only README.txt, found: {contents}")
    idf_path = os.environ.get("IDF_PATH")
    if not idf_path:
        raise ValueError("activate ESP-IDF 5.5.3 first (IDF_PATH is not set)")
    generator = Path(idf_path) / "components/spiffs/spiffsgen.py"
    if not generator.is_file():
        raise ValueError(f"SPIFFS generator missing: {generator}")
    with tempfile.TemporaryDirectory(prefix="gb-clean-saves-") as scratch:
        output = Path(scratch) / "saves.bin"
        subprocess.run(
            [sys.executable, str(generator), "0xf0000", str(source), str(output),
             "--page-size=256", "--obj-name-len=32", "--meta-len=4",
             "--use-magic", "--use-magic-len"],
            check=True,
        )
        return output.read_bytes()


def inputs(build_dir: Path, project_root: Path) -> tuple[dict[str, int], dict[str, bytes]]:
    flash_args = (build_dir / "flash_args").read_text(encoding="utf-8")
    if "--flash_size 8MB" not in flash_args:
        raise ValueError("build does not target 8 MB Flash")
    offsets = parse_flash_args(flash_args)
    if set(offsets) != set(REQUIRED_IMAGES):
        raise ValueError(f"unexpected firmware images: {offsets}")
    if offsets != {
        "bootloader/bootloader.bin": 0x0,
        "partition_table/partition-table.bin": 0x8000,
        "FoloToy-AI-Passport.bin": 0x10000,
    }:
        raise ValueError(f"unexpected firmware offsets: {offsets}")
    images = {name: (build_dir / name).read_bytes() for name in REQUIRED_IMAGES}
    partitions, md5_ok = parse_partition_table(images["partition_table/partition-table.bin"])
    actual = tuple((part.label, part.kind, part.subtype, part.offset, part.size) for part in partitions)
    if not md5_ok or actual != EXPECTED_PARTITIONS:
        raise ValueError(f"unexpected Game Boy partition table: {actual}")
    saves = (build_dir / "saves.bin").read_bytes()
    if len(saves) != 0xF0000 or saves != clean_saves_image(project_root):
        raise ValueError("built saves image is not the reproducible clean initial image")
    images["saves.bin"] = saves
    for name in REQUIRED_IMAGES:
        offset = offsets[name]
        limit = 0x8000 if offset == 0 else 0x9000 if offset == 0x8000 else 0x310000
        if offset + len(images[name]) > limit:
            raise ValueError(f"{name} exceeds its reserved Flash region")
    return offsets, images


def validate_rom_image(rom_image: bytes) -> list[str]:
    names = inspect_image(rom_image)
    if len(rom_image) > ROM_DATA_SIZE:
        raise ValueError("preinstalled ROM overlaps the two writable directory sectors")
    return names


def expected_image(offsets: dict[str, int], images: dict[str, bytes],
                   rom_image: bytes | None = None) -> bytes:
    image = bytearray(b"\xff" * FLASH_SIZE)
    for name in REQUIRED_IMAGES:
        offset = offsets[name]
        image[offset:offset + len(images[name])] = images[name]
    image[0x710000:0x800000] = images["saves.bin"]
    if rom_image is not None:
        validate_rom_image(rom_image)
        image[ROM_OFFSET:ROM_OFFSET + len(rom_image)] = rom_image
    return bytes(image)


def validate(image: bytes, build_dir: Path, offsets: dict[str, int], images: dict[str, bytes],
             rom_image: bytes | None = None) -> str:
    if len(image) != FLASH_SIZE:
        raise ValueError(f"release image must be exactly {FLASH_SIZE} bytes")
    for name in REQUIRED_IMAGES:
        offset = offsets[name]
        if image[offset:offset + len(images[name])] != images[name]:
            raise ValueError(f"release image differs from {name}")
    verify_firmware_layout(image, build_dir, 0x8000, 0x10000)
    if image != expected_image(offsets, images, rom_image):
        raise ValueError("release image contains unexpected bytes, ROMs, or user data")
    return sha256(image)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, default=Path("build"))
    parser.add_argument("--output", type=Path, help="output image; defaults to clean/preloaded filename")
    parser.add_argument("--rom", type=Path, action="append", help="local ROM to preinstall; repeat for multiple games")
    parser.add_argument("--rom-name", action="append", help="game-list name (UTF-8, at most 31 bytes); provide one per --rom, in order")
    parser.add_argument("--verify", action="store_true", help="verify an existing release image")
    args = parser.parse_args()
    if args.rom_name is not None and args.rom is None:
        parser.error("--rom-name requires --rom")
    if args.rom_name is not None and len(args.rom_name) != len(args.rom):
        parser.error("provide one --rom-name per --rom, in the same order")
    project_root = Path(__file__).resolve().parent.parent
    build_dir = args.build_dir.resolve()
    output = (args.output or (PRELOADED_OUTPUT if args.rom else DEFAULT_OUTPUT)).resolve()
    try:
        rom_image = None
        if args.rom:
            rom_image = pack(args.rom, args.rom_name)
            validate_rom_image(rom_image)
        offsets, images = inputs(build_dir, project_root)
        if args.verify:
            image = output.read_bytes()
        else:
            image = expected_image(offsets, images, rom_image)
        digest = validate(image, build_dir, offsets, images, rom_image)
        checksum_path = output.with_suffix(output.suffix + ".sha256")
        checksum_line = f"{digest}  {output.name}\n"
        if args.verify and checksum_path.read_text(encoding="ascii") != checksum_line:
            raise ValueError("release checksum sidecar does not match the image")
        if not args.verify:
            output.parent.mkdir(parents=True, exist_ok=True)
            with tempfile.NamedTemporaryFile(dir=output.parent, prefix=".gameboy-release-", delete=False) as stream:
                temporary = Path(stream.name)
                stream.write(image)
            try:
                # CI 在容器内生成镜像，宿主机仍需能读取它以上传构建产物。
                temporary.chmod(0o644)
                temporary.replace(output)
            finally:
                temporary.unlink(missing_ok=True)
            checksum_path.write_text(checksum_line, encoding="ascii")
        kind = "preloaded" if rom_image is not None else "clean"
        print(f"8 MiB {kind} release: PASS ({len(image)} bytes, SHA-256 {digest})")
        if rom_image is not None:
            print(f"Preinstalled games: {', '.join(inspect_image(rom_image))}")
        print(f"Image: {output}")
        return 0
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
