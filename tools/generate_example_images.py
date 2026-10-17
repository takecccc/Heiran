from __future__ import annotations

import binascii
import struct
import zlib
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
EXAMPLES_DIR = ROOT / "examples" / "images"
WIDTH = 320
HEIGHT = 240


def png_chunk(chunk_type: bytes, data: bytes) -> bytes:
    crc = binascii.crc32(chunk_type)
    crc = binascii.crc32(data, crc)
    return struct.pack(">I", len(data)) + chunk_type + data + struct.pack(">I", crc & 0xFFFFFFFF)


def save_png(path: Path, pixels: list[list[tuple[int, int, int]]]) -> None:
    raw = bytearray()
    for row in pixels:
        raw.append(0)
        for red, green, blue in row:
            raw.extend((red, green, blue))

    ihdr = struct.pack(">IIBBBBB", WIDTH, HEIGHT, 8, 2, 0, 0, 0)
    compressed = zlib.compress(bytes(raw), level=9)
    png = b"\x89PNG\r\n\x1a\n"
    png += png_chunk(b"IHDR", ihdr)
    png += png_chunk(b"IDAT", compressed)
    png += png_chunk(b"IEND", b"")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(png)


def blank(color: tuple[int, int, int]) -> list[list[tuple[int, int, int]]]:
    return [[color for _ in range(WIDTH)] for _ in range(HEIGHT)]


def clamp(value: int) -> int:
    return max(0, min(255, value))


def tint(color: tuple[int, int, int], delta: int) -> tuple[int, int, int]:
    return tuple(clamp(channel + delta) for channel in color)


def draw_rect(
    pixels: list[list[tuple[int, int, int]]],
    left: int,
    top: int,
    width: int,
    height: int,
    color: tuple[int, int, int],
) -> None:
    right = min(WIDTH, left + width)
    bottom = min(HEIGHT, top + height)
    for y in range(max(0, top), bottom):
        row = pixels[y]
        for x in range(max(0, left), right):
            row[x] = color


def draw_frame(
    pixels: list[list[tuple[int, int, int]]], inset: int, thickness: int, color: tuple[int, int, int]
) -> None:
    draw_rect(pixels, inset, inset, WIDTH - inset * 2, thickness, color)
    draw_rect(pixels, inset, HEIGHT - inset - thickness, WIDTH - inset * 2, thickness, color)
    draw_rect(pixels, inset, inset, thickness, HEIGHT - inset * 2, color)
    draw_rect(pixels, WIDTH - inset - thickness, inset, thickness, HEIGHT - inset * 2, color)


def draw_circle(
    pixels: list[list[tuple[int, int, int]]], center_x: int, center_y: int, radius: int, color: tuple[int, int, int]
) -> None:
    radius_sq = radius * radius
    for y in range(max(0, center_y - radius), min(HEIGHT, center_y + radius + 1)):
        row = pixels[y]
        dy = y - center_y
        for x in range(max(0, center_x - radius), min(WIDTH, center_x + radius + 1)):
            dx = x - center_x
            if dx * dx + dy * dy <= radius_sq:
                row[x] = color


def draw_line(
    pixels: list[list[tuple[int, int, int]]],
    x0: int,
    y0: int,
    x1: int,
    y1: int,
    color: tuple[int, int, int],
    thickness: int = 1,
) -> None:
    dx = abs(x1 - x0)
    dy = -abs(y1 - y0)
    sx = 1 if x0 < x1 else -1
    sy = 1 if y0 < y1 else -1
    error = dx + dy

    while True:
        draw_rect(pixels, x0 - thickness // 2, y0 - thickness // 2, thickness, thickness, color)
        if x0 == x1 and y0 == y1:
            break
        doubled = 2 * error
        if doubled >= dy:
            error += dy
            x0 += sx
        if doubled <= dx:
            error += dx
            y0 += sy


def draw_vertical_bars(
    pixels: list[list[tuple[int, int, int]]], page: int, base: tuple[int, int, int], accent: tuple[int, int, int]
) -> None:
    bar_width = 28
    gap = 16
    left = 36 + page * 10
    heights = [80, 120, 60, 140, 100]
    for index, height in enumerate(heights):
        draw_rect(
            pixels,
            left + index * (bar_width + gap),
            HEIGHT - 36 - height,
            bar_width,
            height,
            tint(base, index * 8),
        )
    draw_line(pixels, 32, 40, WIDTH - 32, 40 + page * 18, accent, thickness=4)


def draw_quadrants(
    pixels: list[list[tuple[int, int, int]]], page: int, base: tuple[int, int, int], accent: tuple[int, int, int]
) -> None:
    mid_x = WIDTH // 2
    mid_y = HEIGHT // 2
    draw_rect(pixels, 24, 24, mid_x - 32, mid_y - 32, tint(base, -20))
    draw_rect(pixels, mid_x + 8, 24, mid_x - 32, mid_y - 32, tint(base, 15))
    draw_rect(pixels, 24, mid_y + 8, mid_x - 32, mid_y - 32, tint(base, 35))
    draw_rect(pixels, mid_x + 8, mid_y + 8, mid_x - 32, mid_y - 32, tint(base, -40))
    draw_circle(pixels, 70 + page * 40, 70 + page * 18, 22, accent)
    draw_circle(pixels, WIDTH - 70 - page * 25, HEIGHT - 64, 18, tint(accent, -30))


def draw_diagonal_shapes(
    pixels: list[list[tuple[int, int, int]]], page: int, base: tuple[int, int, int], accent: tuple[int, int, int]
) -> None:
    draw_line(pixels, 32, HEIGHT - 40, WIDTH - 32, 36, accent, thickness=5)
    draw_line(pixels, 32, 36 + page * 14, WIDTH - 32, HEIGHT - 40 - page * 10, tint(base, 40), thickness=3)
    draw_rect(pixels, 48 + page * 18, 72, 64, 64, tint(base, -15))
    draw_rect(pixels, WIDTH - 132, 110 + page * 12, 72, 72, tint(base, 28))
    draw_circle(pixels, WIDTH // 2, HEIGHT // 2, 26 + page * 6, tint(accent, -10))


def draw_grid(
    pixels: list[list[tuple[int, int, int]]], page: int, base: tuple[int, int, int], accent: tuple[int, int, int]
) -> None:
    step = 24
    for x in range(24, WIDTH - 24, step):
        draw_rect(pixels, x, 24, 2, HEIGHT - 48, tint(base, -30))
    for y in range(24, HEIGHT - 24, step):
        draw_rect(pixels, 24, y, WIDTH - 48, 2, tint(base, -30))
    draw_circle(pixels, 70 + page * 24, HEIGHT // 2, 18, accent)
    draw_circle(pixels, 140 + page * 22, HEIGHT // 2 - 28, 18, tint(accent, -20))
    draw_circle(pixels, 210 + page * 20, HEIGHT // 2 + 24, 18, tint(accent, 20))


def build_image(set_name: str, page: int) -> list[list[tuple[int, int, int]]]:
    themes = {
        "a": ((242, 244, 247), (210, 78, 64), (106, 36, 28), draw_vertical_bars),
        "b": ((242, 246, 240), (72, 150, 92), (33, 79, 45), draw_quadrants),
        "c": ((239, 244, 249), (64, 114, 196), (28, 56, 108), draw_diagonal_shapes),
        "d": ((247, 243, 236), (206, 142, 44), (104, 69, 19), draw_grid),
    }
    background, base, accent, painter = themes[set_name]
    pixels = blank(background)
    draw_frame(pixels, 12, 8, tint(base, 10))
    draw_rect(pixels, 20, HEIGHT - 28, WIDTH - 40, 8, tint(base, -10))
    painter(pixels, page, base, accent)
    return pixels


def main() -> None:
    for set_name in ("a", "b", "c", "d"):
        for page in range(3):
            image_path = EXAMPLES_DIR / set_name / f"{page + 1:04d}.png"
            save_png(image_path, build_image(set_name, page))
            print(f"generated {image_path.relative_to(ROOT)}")


if __name__ == "__main__":
    main()
