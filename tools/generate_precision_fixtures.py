"""Generate high bit depth fixtures for checking the analysis buffer.

The example images under ``examples/images`` are 8bit PNG, which never exercise
the native precision path. This script writes a 16bit PNG and a 32bit float
OpenEXR so that the uint16 and float branches can be checked by hand.

Both writers are dependency free. The EXR is written uncompressed.
"""

from __future__ import annotations

import binascii
import struct
import sys
import zlib
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
DEFAULT_OUTPUT_DIR = ROOT / "examples" / "precision"
WIDTH = 64
HEIGHT = 48


def png_chunk(chunk_type: bytes, data: bytes) -> bytes:
    crc = binascii.crc32(chunk_type)
    crc = binascii.crc32(data, crc)
    return struct.pack(">I", len(data)) + chunk_type + data + struct.pack(">I", crc & 0xFFFFFFFF)


def save_png16(path: Path, pixel_fn) -> None:
    """Write a 16bit RGB PNG. ``pixel_fn`` returns values in 0-65535."""
    raw = bytearray()
    for y in range(HEIGHT):
        raw.append(0)
        for x in range(WIDTH):
            raw.extend(struct.pack(">HHH", *pixel_fn(x, y)))

    ihdr = struct.pack(">IIBBBBB", WIDTH, HEIGHT, 16, 2, 0, 0, 0)
    png = b"\x89PNG\r\n\x1a\n"
    png += png_chunk(b"IHDR", ihdr)
    png += png_chunk(b"IDAT", zlib.compress(bytes(raw), level=9))
    png += png_chunk(b"IEND", b"")
    path.write_bytes(png)


def exr_attribute(name: str, type_name: str, data: bytes) -> bytes:
    return name.encode() + b"\x00" + type_name.encode() + b"\x00" + struct.pack("<i", len(data)) + data


def save_exr_float(path: Path, pixel_fn) -> None:
    """Write an uncompressed 32bit float RGB OpenEXR.

    ``pixel_fn`` returns linear float values, which may exceed 1.0.
    Channels are stored in alphabetical order as the format requires.
    """
    channel_list = b""
    for channel in ("B", "G", "R"):
        channel_list += channel.encode() + b"\x00"
        channel_list += struct.pack("<i", 2)  # FLOAT
        channel_list += struct.pack("<B", 0)  # pLinear
        channel_list += b"\x00\x00\x00"  # reserved
        channel_list += struct.pack("<ii", 1, 1)  # xSampling, ySampling
    channel_list += b"\x00"

    header = b""
    header += exr_attribute("channels", "chlist", channel_list)
    header += exr_attribute("compression", "compression", struct.pack("<B", 0))
    header += exr_attribute("dataWindow", "box2i", struct.pack("<iiii", 0, 0, WIDTH - 1, HEIGHT - 1))
    header += exr_attribute(
        "displayWindow", "box2i", struct.pack("<iiii", 0, 0, WIDTH - 1, HEIGHT - 1)
    )
    header += exr_attribute("lineOrder", "lineOrder", struct.pack("<B", 0))
    header += exr_attribute("pixelAspectRatio", "float", struct.pack("<f", 1.0))
    header += exr_attribute("screenWindowCenter", "v2f", struct.pack("<ff", 0.0, 0.0))
    header += exr_attribute("screenWindowWidth", "float", struct.pack("<f", 1.0))
    header += b"\x00"

    rows = []
    for y in range(HEIGHT):
        blue = b""
        green = b""
        red = b""
        for x in range(WIDTH):
            r, g, b = pixel_fn(x, y)
            red += struct.pack("<f", r)
            green += struct.pack("<f", g)
            blue += struct.pack("<f", b)
        rows.append(blue + green + red)

    magic = struct.pack("<I", 20000630) + struct.pack("<I", 2)
    data_start = len(magic) + len(header) + 8 * HEIGHT

    offsets = b""
    position = data_start
    for row in rows:
        offsets += struct.pack("<Q", position)
        position += 8 + len(row)  # y and size, then the scanline

    body = b""
    for y, row in enumerate(rows):
        body += struct.pack("<ii", y, len(row)) + row

    path.write_bytes(magic + header + offsets + body)


def main() -> None:
    output_dir = Path(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT_OUTPUT_DIR
    output_dir.mkdir(parents=True, exist_ok=True)

    # Neighbouring values differ only in the low bits, so quantising to 8bit
    # collapses them. Useful for checking that precision survives.
    save_png16(output_dir / "gradient16.png", lambda x, y: (x * 1024, y * 1024, 30000))
    save_png16(output_dir / "gradient16_shifted.png", lambda x, y: (x * 1024 + 200, y * 1024 + 200, 30000))

    # The red channel reaches 4.0 so the 1.0 clamp on the display path is visible.
    save_exr_float(output_dir / "hdr.exr", lambda x, y: (x / (WIDTH - 1) * 4.0, y / (HEIGHT - 1), 0.5))
    save_exr_float(
        output_dir / "hdr_shifted.exr",
        lambda x, y: (x / (WIDTH - 1) * 4.0 + 0.01, y / (HEIGHT - 1), 0.5),
    )

    print(f"wrote fixtures to {output_dir}")


if __name__ == "__main__":
    main()
