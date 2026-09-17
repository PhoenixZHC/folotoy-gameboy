"""Pack locally owned GB ROMs into the FoloToy Flash ROM partition image."""

from __future__ import annotations

import argparse
import hashlib
import struct
from pathlib import Path

MAX_IMAGE = 0x400000
MAX_ROMS = 16
ENTRY = struct.Struct("<32sII32s")
SUPPORTED_TYPES = {0x00, 0x01, 0x02, 0x03, 0x05, 0x06,
                   0x0F, 0x10, 0x11, 0x12, 0x13,
                   0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E}


def aligned(value: int, boundary: int = 4096) -> int:
    return (value + boundary - 1) // boundary * boundary


def declared_size(code: int) -> int:
    if code <= 7:
        return 32768 << code
    return {0x52: 72 * 16384, 0x53: 80 * 16384, 0x54: 96 * 16384}.get(code, 0)


def pack(paths: list[Path]) -> bytes:
    if not 1 <= len(paths) <= MAX_ROMS:
        raise ValueError(f"Provide 1 to {MAX_ROMS} ROMs")
    image = bytearray(b"\xff" * 4096)
    entries = []
    names = set()
    for path in paths:
        data = path.read_bytes()
        if len(data) < 32768 or len(data) > MAX_IMAGE or len(data) % 16384:
            raise ValueError(f"Invalid ROM length: {path}")
        if (declared_size(data[0x148]) != len(data) or data[0x143] == 0xC0
                or data[0x147] not in SUPPORTED_TYPES):
            raise ValueError(f"Unsupported ROM header or CGB-only cartridge: {path}")
        name = path.stem.encode("utf-8")
        if not name or len(name) > 31 or name in names:
            raise ValueError(f"Invalid or duplicate ROM name: {path}")
        names.add(name)
        offset = aligned(len(image))
        if offset + len(data) > MAX_IMAGE:
            raise ValueError("ROM partition is full")
        image.extend(b"\xff" * (offset - len(image)))
        image.extend(data)
        entries.append(ENTRY.pack(name + b"\0" * (32 - len(name)), offset,
                                  len(data), hashlib.sha256(data).digest()))
    header = struct.pack("<4sHH", b"FGBR", 1, len(entries))
    directory = header + b"".join(entries)
    image[:len(directory)] = directory
    return bytes(image)


def inspect_image(data: bytes) -> list[str]:
    if len(data) > MAX_IMAGE or len(data) < 4096 or data[:4] != b"FGBR":
        raise ValueError("Invalid ROM container")
    version, count = struct.unpack_from("<HH", data, 4)
    if version != 1 or not 1 <= count <= MAX_ROMS or 8 + count * ENTRY.size > 4096:
        raise ValueError("Invalid ROM directory")
    names = []
    ranges = []
    for i in range(count):
        raw_name, offset, size, digest = ENTRY.unpack_from(data, 8 + i * ENTRY.size)
        if b"\0" not in raw_name or not raw_name[0] or offset % 4096:
            raise ValueError(f"Invalid ROM entry {i}")
        end = offset + size
        if offset < 4096 or size < 32768 or size % 16384 or end > len(data):
            raise ValueError(f"ROM {i} outside image")
        if any(offset < old_end and old_start < end for old_start, old_end in ranges):
            raise ValueError(f"ROM {i} overlaps another")
        rom = data[offset:end]
        if hashlib.sha256(rom).digest() != digest:
            raise ValueError(f"ROM {i} hash mismatch")
        if (declared_size(rom[0x148]) != size or rom[0x143] == 0xC0
                or rom[0x147] not in SUPPORTED_TYPES):
            raise ValueError(f"ROM {i} unsupported")
        names.append(raw_name.split(b"\0", 1)[0].decode("utf-8"))
        ranges.append((offset, end))
    return names


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("rom", nargs="*", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--verify", type=Path, help="Verify an existing image")
    args = parser.parse_args()
    if args.verify:
        if args.rom or args.output:
            parser.error("--verify cannot be combined with ROM inputs or --output")
        names = inspect_image(args.verify.read_bytes())
        print(f"Verified {len(names)} ROM(s): {', '.join(names)}")
        return
    if not args.output:
        parser.error("--output is required when packing ROMs")
    data = pack(args.rom)
    inspect_image(data)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(data)
    print(f"Wrote {len(args.rom)} ROM(s), {len(data)} bytes: {args.output}")


if __name__ == "__main__":
    main()
