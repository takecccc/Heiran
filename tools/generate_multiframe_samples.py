"""Generate multi page TIFF and animated GIF samples for trying the sub image controls.

Both writers are dependency free, like ``generate_precision_fixtures.py``.

* ``pages.tif``: 5 pages, each with a different hue and a large page number.
  Use ``Ctrl+Left`` / ``Ctrl+Right`` to turn the pages.
* ``animation.gif``: 12 frames of a square that moves and changes colour.
  It plays by itself when opened directly. ``P`` pauses it.

The output goes to ``examples/multiframe`` and is not committed.
"""

from __future__ import annotations

import colorsys
import struct
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
DEFAULT_OUTPUT_DIR = ROOT / "examples" / "multiframe"

# A 5x7 bitmap font for the digits, one string per row.
DIGITS = {
    "0": ["01110", "10001", "10011", "10101", "11001", "10001", "01110"],
    "1": ["00100", "01100", "00100", "00100", "00100", "00100", "01110"],
    "2": ["01110", "10001", "00001", "00010", "00100", "01000", "11111"],
    "3": ["11110", "00001", "00001", "01110", "00001", "00001", "11110"],
    "4": ["00010", "00110", "01010", "10010", "11111", "00010", "00010"],
    "5": ["11111", "10000", "11110", "00001", "00001", "10001", "01110"],
    "6": ["00110", "01000", "10000", "11110", "10001", "10001", "01110"],
    "7": ["11111", "00001", "00010", "00100", "01000", "01000", "01000"],
    "8": ["01110", "10001", "10001", "01110", "10001", "10001", "01110"],
    "9": ["01110", "10001", "10001", "01111", "00001", "00010", "01100"],
}


def hsv_to_rgb(hue: float, saturation: float, value: float) -> tuple[int, int, int]:
    red, green, blue = colorsys.hsv_to_rgb(hue, saturation, value)
    return int(red * 255), int(green * 255), int(blue * 255)


def draw_number(pixels: list[list[tuple[int, int, int]]], text: str, x0: int, y0: int, scale: int,
                color: tuple[int, int, int]) -> None:
    """Draw ``text`` into ``pixels`` with its top left corner at (x0, y0)."""
    cursor = x0
    for character in text:
        glyph = DIGITS[character]
        for row, bits in enumerate(glyph):
            for column, bit in enumerate(bits):
                if bit != "1":
                    continue
                for dy in range(scale):
                    for dx in range(scale):
                        y = y0 + row * scale + dy
                        x = cursor + column * scale + dx
                        if 0 <= y < len(pixels) and 0 <= x < len(pixels[0]):
                            pixels[y][x] = color
        cursor += 6 * scale


def make_page(width: int, height: int, page: int, page_count: int) -> list[list[tuple[int, int, int]]]:
    """A gradient in the page's hue with the page number in the middle."""
    hue = (page - 1) / page_count
    pixels = []
    for y in range(height):
        row = []
        for x in range(width):
            # Brighter towards the bottom right, so the orientation is visible.
            value = 0.45 + 0.4 * ((x / width) + (y / height)) / 2
            row.append(hsv_to_rgb(hue, 0.65, value))
        pixels.append(row)
    scale = height // 9
    text = str(page)
    text_width = len(text) * 6 * scale - scale
    draw_number(pixels, text, (width - text_width) // 2, (height - 7 * scale) // 2, scale, (255, 255, 255))
    # A small "n / N" caption in the corner.
    draw_number(pixels, f"{page}", 12, 12, 4, (0, 0, 0))
    return pixels


def tiff_ifd_entry(tag: int, kind: int, count: int, value: int) -> bytes:
    return struct.pack("<HHII", tag, kind, count, value)


def save_multipage_tiff(path: Path, pages: list[list[list[tuple[int, int, int]]]]) -> None:
    """Write an uncompressed 8bit RGB TIFF with one IFD per page."""
    entry_count = 9
    ifd_bytes = 2 + entry_count * 12 + 4

    pixel_offsets: list[int] = []
    depth_offsets: list[int] = []
    ifd_offsets: list[int] = []
    cursor = 8
    for page in pages:
        pixel_offsets.append(cursor)
        cursor += len(page) * len(page[0]) * 3
        depth_offsets.append(cursor)
        cursor += 6
        ifd_offsets.append(cursor)
        cursor += ifd_bytes

    data = bytearray(b"II" + struct.pack("<HI", 42, ifd_offsets[0]))
    for index, page in enumerate(pages):
        height = len(page)
        width = len(page[0])
        raw = bytearray()
        for row in page:
            for red, green, blue in row:
                raw += bytes((red, green, blue))
        data += raw
        data += struct.pack("<HHH", 8, 8, 8)
        data += struct.pack("<H", entry_count)
        data += tiff_ifd_entry(256, 4, 1, width)
        data += tiff_ifd_entry(257, 4, 1, height)
        data += tiff_ifd_entry(258, 3, 3, depth_offsets[index])
        data += tiff_ifd_entry(259, 3, 1, 1)  # no compression
        data += tiff_ifd_entry(262, 3, 1, 2)  # RGB
        data += tiff_ifd_entry(273, 4, 1, pixel_offsets[index])
        data += tiff_ifd_entry(277, 3, 1, 3)
        data += tiff_ifd_entry(278, 4, 1, height)
        data += tiff_ifd_entry(279, 4, 1, len(raw))
        next_ifd = ifd_offsets[index + 1] if index + 1 < len(pages) else 0
        data += struct.pack("<I", next_ifd)
    path.write_bytes(bytes(data))


def gif_lzw(indices: bytes) -> bytes:
    """LZW with 8 bit indices. A clear code every two pixels keeps every code 9 bits wide.

    It does not compress, but it is a valid GIF stream and needs no dictionary.
    """
    clear = 256
    end = 257
    packed = bytearray()
    bit_buffer = 0
    bit_count = 0

    def emit(code: int) -> None:
        nonlocal bit_buffer, bit_count
        bit_buffer |= code << bit_count
        bit_count += 9
        while bit_count >= 8:
            packed.append(bit_buffer & 0xFF)
            bit_buffer >>= 8
            bit_count -= 8

    for position, index in enumerate(indices):
        if position % 2 == 0:
            emit(clear)
        emit(index)
    emit(end)
    if bit_count:
        packed.append(bit_buffer & 0xFF)

    blocks = bytearray()
    for offset in range(0, len(packed), 255):
        chunk = packed[offset:offset + 255]
        blocks.append(len(chunk))
        blocks += chunk
    blocks.append(0)
    return bytes(blocks)


def save_animated_gif(path: Path, width: int, height: int, frame_count: int, delay_cs: int) -> None:
    """A square that moves across the frame while the background hue changes."""
    # 64 hues x 4 brightness levels fill the 256 entry palette.
    palette = bytearray()
    for hue_index in range(64):
        for level in range(4):
            palette += bytes(hsv_to_rgb(hue_index / 64, 0.7, 0.35 + level * 0.2))

    gif = bytearray(b"GIF89a" + struct.pack("<HH", width, height))
    gif += bytes((0xF7, 0, 0))  # 256 entry global colour table
    gif += palette
    # Loop forever.
    gif += b"\x21\xFF\x0BNETSCAPE2.0\x03\x01\x00\x00\x00"

    square = height // 4
    for frame in range(frame_count):
        hue_index = (frame * 5) % 64
        square_x = int((width - square) * frame / max(1, frame_count - 1))
        square_y = int((height - square) * (0.5 + 0.4 * (1 if frame % 2 == 0 else -1)) / 1.0) // 2
        indices = bytearray()
        for y in range(height):
            for x in range(width):
                inside = square_x <= x < square_x + square and square_y <= y < square_y + square
                level = 3 if inside else (y * 2) // height
                hue = (hue_index + (32 if inside else 0)) % 64
                indices.append(hue * 4 + level)
        gif += b"\x21\xF9\x04" + bytes((0x04,)) + struct.pack("<H", delay_cs) + b"\x00\x00"
        gif += b"\x2C" + struct.pack("<HHHH", 0, 0, width, height) + b"\x00"
        gif += bytes((8,)) + gif_lzw(bytes(indices))
    gif += b"\x3B"
    path.write_bytes(bytes(gif))


def main() -> None:
    output_dir = Path(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT_OUTPUT_DIR
    output_dir.mkdir(parents=True, exist_ok=True)

    page_count = 5
    pages = [make_page(640, 480, page, page_count) for page in range(1, page_count + 1)]
    save_multipage_tiff(output_dir / "pages.tif", pages)
    save_animated_gif(output_dir / "animation.gif", 240, 180, 12, 8)

    print(f"wrote samples to {output_dir}")


if __name__ == "__main__":
    main()
