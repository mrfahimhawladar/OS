#!/usr/bin/env python3
"""Convert the P6 PPM frames emitted by the native LockScreenRenderer to PNG."""
from __future__ import annotations

import binascii
import pathlib
import struct
import sys
import zlib


def read_ppm(path: pathlib.Path) -> tuple[int, int, bytes]:
    data = path.read_bytes()
    position = 0

    def token() -> bytes:
        nonlocal position
        while position < len(data):
            if data[position] in b" \t\r\n":
                position += 1
            elif data[position] == ord("#"):
                while position < len(data) and data[position] not in b"\r\n":
                    position += 1
            else:
                break
        start = position
        while position < len(data) and data[position] not in b" \t\r\n#":
            position += 1
        if start == position:
            raise ValueError("Malformed PPM header")
        return data[start:position]

    if token() != b"P6":
        raise ValueError("Only binary P6 PPM is supported")
    width = int(token())
    height = int(token())
    maximum = int(token())
    if maximum != 255 or width < 1 or height < 1:
        raise ValueError("Unsupported PPM dimensions or bit depth")
    if position >= len(data) or data[position] not in b" \t\r\n":
        raise ValueError("Missing PPM header separator")
    # Consume exactly one separator; pixel byte values may themselves be whitespace.
    if data[position:position + 2] == b"\r\n":
        position += 2
    else:
        position += 1
    pixels = data[position:]
    expected = width * height * 3
    if len(pixels) != expected:
        raise ValueError(f"Expected {expected} pixel bytes, got {len(pixels)}")
    return width, height, pixels


def chunk(kind: bytes, payload: bytes) -> bytes:
    body = kind + payload
    return struct.pack(">I", len(payload)) + body + struct.pack(">I", binascii.crc32(body) & 0xffffffff)


def write_png(path: pathlib.Path, width: int, height: int, pixels: bytes) -> None:
    stride = width * 3
    scanlines = b"".join(
        b"\x00" + pixels[row * stride:(row + 1) * stride]
        for row in range(height)
    )
    png = (
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
        + chunk(b"IDAT", zlib.compress(scanlines, level=9))
        + chunk(b"IEND", b"")
    )
    path.write_bytes(png)


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: ppm_to_png.py INPUT.ppm OUTPUT.png", file=sys.stderr)
        return 2
    width, height, pixels = read_ppm(pathlib.Path(sys.argv[1]))
    write_png(pathlib.Path(sys.argv[2]), width, height, pixels)
    print(f"Wrote {sys.argv[2]} ({width}x{height})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
