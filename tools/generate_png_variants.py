"""Generate PNG variants for comparing decoder backends.

The example images are all 8bit truecolour, which never exercises palette,
grayscale, low bit depths or interlacing. A decoder can be fast on that one
shape and wrong everywhere else, so generate the other shapes too and compare
the decoded pixels between backends.

No third party module is used. Everything is written by hand.
"""

from __future__ import annotations

import binascii
import struct
import sys
import zlib
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
DEFAULT_OUTPUT_DIR = ROOT / "examples" / "pngvariants"
WIDTH = 61
HEIGHT = 37


def png_chunk(chunk_type: bytes, data: bytes) -> bytes:
    crc = binascii.crc32(chunk_type)
    crc = binascii.crc32(data, crc)
    return struct.pack(">I", len(data)) + chunk_type + data + struct.pack(">I", crc & 0xFFFFFFFF)


def pack_scanline(values: list[int], bit_depth: int) -> bytes:
    """Pack sample values into a scanline for bit depths below 8."""
    if bit_depth == 8:
        return bytes(values)
    if bit_depth == 16:
        return b"".join(struct.pack(">H", value) for value in values)

    per_byte = 8 // bit_depth
    mask = (1 << bit_depth) - 1
    out = bytearray()
    for index in range(0, len(values), per_byte):
        packed = 0
        for slot in range(per_byte):
            sample = values[index + slot] & mask if index + slot < len(values) else 0
            packed |= sample << (8 - bit_depth * (slot + 1))
        out.append(packed)
    return bytes(out)


def adam7_passes(width: int, height: int) -> list[tuple[list[int], list[int]]]:
    """Return the (columns, rows) covered by each Adam7 pass."""
    starts = [(0, 0, 8, 8), (4, 0, 8, 8), (0, 4, 4, 8), (2, 0, 4, 4),
              (0, 2, 2, 4), (1, 0, 2, 2), (0, 1, 1, 2)]
    passes = []
    for x0, y0, dx, dy in starts:
        columns = list(range(x0, width, dx))
        rows = list(range(y0, height, dy))
        passes.append((columns, rows))
    return passes


def write_png(path: Path, width: int, height: int, bit_depth: int, colour_type: int,
              sample_fn, palette: list[tuple[int, int, int]] | None = None,
              interlace: bool = False) -> None:
    channels = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[colour_type]

    def scanline(y: int, columns: list[int]) -> bytes:
        values: list[int] = []
        for x in columns:
            values.extend(sample_fn(x, y))
        return pack_scanline(values, bit_depth)

    raw = bytearray()
    if interlace:
        for columns, rows in adam7_passes(width, height):
            if not columns or not rows:
                continue
            for y in rows:
                raw.append(0)
                raw += scanline(y, columns)
    else:
        all_columns = list(range(width))
        for y in range(height):
            raw.append(0)
            raw += scanline(y, all_columns)

    png = b"\x89PNG\r\n\x1a\n"
    png += png_chunk(
        b"IHDR",
        struct.pack(">IIBBBBB", width, height, bit_depth, colour_type, 0, 0, 1 if interlace else 0),
    )
    if palette is not None:
        png += png_chunk(b"PLTE", b"".join(bytes(entry) for entry in palette))
    png += png_chunk(b"IDAT", zlib.compress(bytes(raw), 9))
    png += png_chunk(b"IEND", b"")
    path.write_bytes(png)
    print(f"  {path.name}")

    _ = channels


def main() -> None:
    output_dir = Path(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT_OUTPUT_DIR
    output_dir.mkdir(parents=True, exist_ok=True)
    print(f"writing PNG variants to {output_dir}")

    write_png(output_dir / "rgb8.png", WIDTH, HEIGHT, 8, 2,
              lambda x, y: ((x * 4) % 256, (y * 7) % 256, (x + y) % 256))
    write_png(output_dir / "rgb8_interlaced.png", WIDTH, HEIGHT, 8, 2,
              lambda x, y: ((x * 4) % 256, (y * 7) % 256, (x + y) % 256), interlace=True)
    write_png(output_dir / "rgba8.png", WIDTH, HEIGHT, 8, 6,
              lambda x, y: ((x * 4) % 256, (y * 7) % 256, (x + y) % 256, (x * y) % 256))
    write_png(output_dir / "gray8.png", WIDTH, HEIGHT, 8, 0, lambda x, y: ((x + y * 3) % 256,))
    write_png(output_dir / "gray16.png", WIDTH, HEIGHT, 16, 0,
              lambda x, y: ((x * 1000 + y * 37) % 65536,))
    write_png(output_dir / "gray_alpha8.png", WIDTH, HEIGHT, 8, 4,
              lambda x, y: ((x * 3) % 256, (y * 5) % 256))
    write_png(output_dir / "rgb16.png", WIDTH, HEIGHT, 16, 2,
              lambda x, y: ((x * 1000) % 65536, (y * 1500) % 65536, 30000))
    write_png(output_dir / "gray1.png", WIDTH, HEIGHT, 1, 0, lambda x, y: ((x + y) % 2,))
    write_png(output_dir / "gray4.png", WIDTH, HEIGHT, 4, 0, lambda x, y: ((x + y) % 16,))

    palette = [(index * 3 % 256, index * 7 % 256, index * 11 % 256) for index in range(256)]
    write_png(output_dir / "palette8.png", WIDTH, HEIGHT, 8, 3,
              lambda x, y: ((x * 5 + y * 3) % 256,), palette=palette)
    write_png(output_dir / "palette4.png", WIDTH, HEIGHT, 4, 3,
              lambda x, y: ((x + y) % 16,), palette=palette[:16])


if __name__ == "__main__":
    main()
