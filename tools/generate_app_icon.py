"""Generate the Heiran application icon.

The icon has to say "several images, looked at together" at sizes down to
16 pixels. Three layouts are drawn so they can be compared before one is
adopted:

- grid  : four tonal panels with the cursor at the same spot in each one.
- split : two wide panels, the same idea with fewer parts.
- stack : two offset frames, "one image in front of another".

No third party module is used. Everything is written by hand, the same way as
the other generators under tools/.
"""

from __future__ import annotations

import argparse
import binascii
import struct
import sys
import zlib
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
DEFAULT_OUTPUT_DIR = ROOT / "resources"

# Sizes stored in the .ico. Windows picks the closest one for each surface.
ICON_SIZES = (16, 20, 24, 32, 48, 64, 128, 256)

# Supersampling factor. Every output pixel is decided from SCALE x SCALE
# inside/outside tests, which is what gives the edges their smoothness.
SCALE = 4

# The palette follows the application. The tile is the canvas background,
# the panels are the light backdrop drawn under a transparent image.
TILE_TOP = (60, 69, 83)
TILE_BOTTOM = (38, 43, 52)
PANEL_TONES = ((238, 241, 244), (201, 208, 216), (164, 174, 186), (127, 139, 155))
INK = (27, 31, 38)
AMBER = (255, 194, 74)


def lerp(a: float, b: float, t: float) -> float:
    return a + (b - a) * t


def clamp(value: float, low: float, high: float) -> float:
    return low if value < low else high if value > high else value


class Canvas:
    """RGBA canvas at the supersampled resolution.

    Coordinates are normalized to 0.0-1.0 so a layout can be written once and
    drawn at any size.
    """

    def __init__(self, size: int) -> None:
        self.size = size
        self.n = size * SCALE
        self.pixels = bytearray(self.n * self.n * 4)

    def _blend(self, index: int, color: tuple[int, int, int], alpha: float) -> None:
        if alpha <= 0.0:
            return
        offset = index * 4
        pixels = self.pixels
        old_alpha = pixels[offset + 3] / 255.0
        new_alpha = alpha + old_alpha * (1.0 - alpha)
        if new_alpha <= 0.0:
            return
        for channel in range(3):
            source = color[channel]
            destination = pixels[offset + channel]
            value = (source * alpha + destination * old_alpha * (1.0 - alpha)) / new_alpha
            pixels[offset + channel] = int(clamp(value + 0.5, 0.0, 255.0))
        pixels[offset + 3] = int(clamp(new_alpha * 255.0 + 0.5, 0.0, 255.0))

    def fill_rounded_rect(
        self,
        x0: float,
        y0: float,
        x1: float,
        y1: float,
        radius: float,
        color: tuple[int, int, int] | None = None,
        gradient: tuple[tuple[int, int, int], tuple[int, int, int]] | None = None,
    ) -> None:
        n = self.n
        left, top = x0 * n, y0 * n
        right, bottom = x1 * n, y1 * n
        r = radius * n
        # Only walk the bounding box. Scanning the whole canvas per shape is
        # what makes a pure Python rasterizer too slow to use.
        for py in range(max(0, int(top)), min(n, int(bottom) + 1)):
            sample_y = py + 0.5
            if gradient is not None:
                row_color = tuple(
                    int(lerp(gradient[0][c], gradient[1][c], sample_y / n)) for c in range(3)
                )
            else:
                row_color = color
            row_index = py * n
            for px in range(max(0, int(left)), min(n, int(right) + 1)):
                sample_x = px + 0.5
                if not _inside_rounded_rect(sample_x, sample_y, left, top, right, bottom, r):
                    continue
                self._blend(row_index + px, row_color, 1.0)

    def fill_capsule(
        self,
        x0: float,
        y0: float,
        x1: float,
        y1: float,
        thickness: float,
        color: tuple[int, int, int],
    ) -> None:
        """A line segment with round caps. Used for the cursor arms."""
        n = self.n
        ax, ay = x0 * n, y0 * n
        bx, by = x1 * n, y1 * n
        half = thickness * n * 0.5
        for py in range(max(0, int(min(ay, by) - half)), min(n, int(max(ay, by) + half) + 1)):
            row_index = py * n
            sample_y = py + 0.5
            for px in range(max(0, int(min(ax, bx) - half)), min(n, int(max(ax, bx) + half) + 1)):
                if _distance_to_segment(px + 0.5, sample_y, ax, ay, bx, by) > half:
                    continue
                self._blend(row_index + px, color, 1.0)

    def fill_circle(self, cx: float, cy: float, radius: float, color: tuple[int, int, int]) -> None:
        self.fill_capsule(cx, cy, cx, cy, radius * 2.0, color)

    def to_rgba(self) -> bytes:
        """Box filter the supersampled canvas down to the output size."""
        size = self.size
        n = self.n
        source = self.pixels
        result = bytearray(size * size * 4)
        samples = SCALE * SCALE
        for y in range(size):
            for x in range(size):
                totals = [0, 0, 0, 0]
                for sub_y in range(SCALE):
                    row_index = ((y * SCALE + sub_y) * n + x * SCALE) * 4
                    for sub_x in range(SCALE):
                        offset = row_index + sub_x * 4
                        alpha = source[offset + 3]
                        # Weight the colour by alpha so transparent samples do
                        # not drag the edge towards black.
                        totals[0] += source[offset] * alpha
                        totals[1] += source[offset + 1] * alpha
                        totals[2] += source[offset + 2] * alpha
                        totals[3] += alpha
                out = (y * size + x) * 4
                if totals[3] == 0:
                    continue
                for channel in range(3):
                    result[out + channel] = int(totals[channel] / totals[3] + 0.5)
                result[out + 3] = int(totals[3] / samples + 0.5)
        return bytes(result)


def _inside_rounded_rect(
    x: float, y: float, left: float, top: float, right: float, bottom: float, radius: float
) -> bool:
    if x < left or x > right or y < top or y > bottom:
        return False
    if radius <= 0.0:
        return True
    corner_x = left + radius if x < left + radius else right - radius if x > right - radius else None
    corner_y = top + radius if y < top + radius else bottom - radius if y > bottom - radius else None
    if corner_x is None or corner_y is None:
        return True
    dx = x - corner_x
    dy = y - corner_y
    return dx * dx + dy * dy <= radius * radius


def _distance_to_segment(x: float, y: float, ax: float, ay: float, bx: float, by: float) -> float:
    dx = bx - ax
    dy = by - ay
    length_squared = dx * dx + dy * dy
    if length_squared == 0.0:
        t = 0.0
    else:
        t = clamp(((x - ax) * dx + (y - ay) * dy) / length_squared, 0.0, 1.0)
    px = ax + dx * t
    py = ay + dy * t
    return ((x - px) ** 2 + (y - py) ** 2) ** 0.5


def draw_tile(canvas: Canvas, coarse: bool) -> None:
    # Small sizes give up the outer margin. Every pixel spent on it is a pixel
    # the panels do not get.
    margin = 0.0 if coarse else 0.02
    radius = 0.16 if coarse else 0.22
    canvas.fill_rounded_rect(
        margin, margin, 1.0 - margin, 1.0 - margin, radius, gradient=(TILE_TOP, TILE_BOTTOM)
    )


def draw_cursor(
    canvas: Canvas,
    cx: float,
    cy: float,
    span: float,
    color: tuple[int, int, int],
    coarse: bool,
) -> None:
    """The shared cursor. Below 32 pixels the arms turn into a dot.

    Four thin lines with a gap in the middle disappear at 16 pixels, and what
    is left reads as dirt rather than as a cursor.
    """
    if coarse:
        canvas.fill_circle(cx, cy, span * 0.75, color)
        return

    arm = span
    gap = span * 0.32
    thickness = span * 0.26
    canvas.fill_capsule(cx - arm, cy, cx - gap, cy, thickness, color)
    canvas.fill_capsule(cx + gap, cy, cx + arm, cy, thickness, color)
    canvas.fill_capsule(cx, cy - arm, cx, cy - gap, thickness, color)
    canvas.fill_capsule(cx, cy + gap, cx, cy + arm, thickness, color)


def draw_grid(canvas: Canvas, coarse: bool) -> None:
    """Four panels, the cursor at the same spot in every one."""
    draw_tile(canvas, coarse)

    left, top, right, bottom = (
        (0.06, 0.06, 0.94, 0.94) if coarse else (0.11, 0.11, 0.89, 0.89)
    )
    gap = 0.10 if coarse else 0.045
    panel = (right - left - gap) / 2.0
    radius = 0.0 if coarse else 0.035
    for index in range(4):
        column, row = index % 2, index // 2
        px = left + column * (panel + gap)
        py = top + row * (panel + gap)
        if index == 0:
            # The active cell carries the selection colour used in the viewer.
            # It is the only colour in the icon, so it is kept at every size.
            # The border goes inside the panel. Drawn outside it, the cell ends
            # up larger than the other three and the grid stops looking regular.
            border = panel * (0.16 if coarse else 0.10)
            canvas.fill_rounded_rect(px, py, px + panel, py + panel, radius, color=AMBER)
            canvas.fill_rounded_rect(
                px + border, py + border, px + panel - border, py + panel - border,
                max(0.0, radius - border), color=PANEL_TONES[index],
            )
        else:
            canvas.fill_rounded_rect(
                px, py, px + panel, py + panel, radius, color=PANEL_TONES[index]
            )
        draw_cursor(canvas, px + panel * 0.40, py + panel * 0.42, panel * 0.32, INK, coarse)


def draw_split(canvas: Canvas, coarse: bool) -> None:
    """Two wide panels. Fewer parts, so it survives smaller sizes."""
    draw_tile(canvas, coarse)

    left, top, right, bottom = (
        (0.06, 0.14, 0.94, 0.86) if coarse else (0.11, 0.19, 0.89, 0.81)
    )
    gap = 0.10 if coarse else 0.05
    panel_width = (right - left - gap) / 2.0
    radius = 0.0 if coarse else 0.035
    for index in range(2):
        px = left + index * (panel_width + gap)
        canvas.fill_rounded_rect(
            px, top, px + panel_width, bottom, radius, color=PANEL_TONES[index * 2]
        )
        draw_cursor(
            canvas,
            px + panel_width * 0.42,
            top + (bottom - top) * 0.45,
            panel_width * 0.30,
            INK,
            coarse,
        )


def draw_stack(canvas: Canvas, coarse: bool) -> None:
    """Two offset frames, the cursor on the front one."""
    draw_tile(canvas, coarse)

    radius = 0.0 if coarse else 0.04
    canvas.fill_rounded_rect(0.32, 0.14, 0.88, 0.62, radius, color=PANEL_TONES[3])
    # A dark plate behind the front frame keeps the two apart without an outline.
    canvas.fill_rounded_rect(0.10, 0.34, 0.72, 0.88, radius, color=TILE_BOTTOM)
    canvas.fill_rounded_rect(0.14, 0.38, 0.68, 0.86, radius, color=PANEL_TONES[0])
    draw_cursor(canvas, 0.34, 0.60, 0.13, INK, coarse)


LAYOUTS = {
    "grid": draw_grid,
    "split": draw_split,
    "stack": draw_stack,
}


def render(layout: str, size: int) -> bytes:
    canvas = Canvas(size)
    # Small sizes get the simplified drawing. Detail that cannot be resolved
    # turns into noise, which looks worse than leaving it out.
    LAYOUTS[layout](canvas, coarse=size <= 32)
    return canvas.to_rgba()


def png_chunk(chunk_type: bytes, data: bytes) -> bytes:
    crc = binascii.crc32(chunk_type)
    crc = binascii.crc32(data, crc)
    return struct.pack(">I", len(data)) + chunk_type + data + struct.pack(">I", crc & 0xFFFFFFFF)


def write_png(path: Path, rgba: bytes, width: int, height: int) -> None:
    raw = bytearray()
    stride = width * 4
    for y in range(height):
        raw.append(0)
        raw += rgba[y * stride : (y + 1) * stride]

    header = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
    data = b"\x89PNG\r\n\x1a\n"
    data += png_chunk(b"IHDR", header)
    data += png_chunk(b"IDAT", zlib.compress(bytes(raw), 9))
    data += png_chunk(b"IEND", b"")
    path.write_bytes(data)


def png_bytes(rgba: bytes, width: int, height: int) -> bytes:
    raw = bytearray()
    stride = width * 4
    for y in range(height):
        raw.append(0)
        raw += rgba[y * stride : (y + 1) * stride]
    header = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
    return (
        b"\x89PNG\r\n\x1a\n"
        + png_chunk(b"IHDR", header)
        + png_chunk(b"IDAT", zlib.compress(bytes(raw), 9))
        + png_chunk(b"IEND", b"")
    )


def dib_bytes(rgba: bytes, size: int) -> bytes:
    """A 32bpp DIB entry for the .ico.

    NSIS does not accept an .ico made only of PNG entries, so everything below
    256 pixels is stored the old way. Rows go bottom-up and an AND mask has to
    follow even when the colour data already carries alpha.
    """
    header = struct.pack(
        "<IiiHHIIiiII", 40, size, size * 2, 1, 32, 0, size * size * 4, 0, 0, 0, 0
    )
    pixels = bytearray()
    for y in range(size - 1, -1, -1):
        for x in range(size):
            offset = (y * size + x) * 4
            pixels += bytes(
                (rgba[offset + 2], rgba[offset + 1], rgba[offset], rgba[offset + 3])
            )

    mask_stride = ((size + 31) // 32) * 4
    mask = bytearray(mask_stride * size)
    return header + bytes(pixels) + bytes(mask)


def write_ico(path: Path, images: list[tuple[int, bytes]]) -> None:
    entries = []
    payloads = []
    offset = 6 + 16 * len(images)
    for size, rgba in images:
        payload = png_bytes(rgba, size, size) if size >= 256 else dib_bytes(rgba, size)
        entries.append(
            struct.pack(
                "<BBBBHHII",
                size if size < 256 else 0,
                size if size < 256 else 0,
                0,
                0,
                1,
                32,
                len(payload),
                offset,
            )
        )
        payloads.append(payload)
        offset += len(payload)

    data = struct.pack("<HHH", 0, 1, len(images)) + b"".join(entries) + b"".join(payloads)
    path.write_bytes(data)


def write_preview(path: Path, layouts: list[str]) -> None:
    """One sheet showing every layout at every size, on a mid grey ground."""
    preview_sizes = (16, 24, 32, 48, 64, 128, 256)
    padding = 12
    row_height = max(preview_sizes) + padding
    width = padding + sum(size + padding for size in preview_sizes)
    height = padding + len(layouts) * row_height

    canvas = bytearray()
    for _ in range(width * height):
        canvas += bytes((128, 132, 138, 255))

    for row, layout in enumerate(layouts):
        x = padding
        baseline = padding + row * row_height + max(preview_sizes)
        for size in preview_sizes:
            rgba = render(layout, size)
            top = baseline - size
            for y in range(size):
                for column in range(size):
                    source = (y * size + column) * 4
                    alpha = rgba[source + 3] / 255.0
                    if alpha <= 0.0:
                        continue
                    target = ((top + y) * width + x + column) * 4
                    for channel in range(3):
                        canvas[target + channel] = int(
                            rgba[source + channel] * alpha
                            + canvas[target + channel] * (1.0 - alpha)
                        )
            x += size + padding

    write_png(path, bytes(canvas), width, height)


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description="Generate the Heiran application icon.")
    parser.add_argument(
        "--layout", choices=sorted(LAYOUTS), default="grid", help="Layout written to the .ico."
    )
    parser.add_argument("--output-dir", type=Path, default=DEFAULT_OUTPUT_DIR)
    parser.add_argument(
        "--preview", action="store_true", help="Also write a sheet comparing every layout."
    )
    arguments = parser.parse_args(argv)

    output_dir = arguments.output_dir
    output_dir.mkdir(parents=True, exist_ok=True)

    images = [(size, render(arguments.layout, size)) for size in ICON_SIZES]
    icon_path = output_dir / "heiran.ico"
    write_ico(icon_path, images)
    print(f"wrote {icon_path} ({icon_path.stat().st_size} bytes, layout={arguments.layout})")

    png_path = output_dir / "heiran-256.png"
    write_png(png_path, images[-1][1], 256, 256)
    print(f"wrote {png_path}")

    if arguments.preview:
        preview_path = output_dir / "icon-preview.png"
        write_preview(preview_path, sorted(LAYOUTS))
        print(f"wrote {preview_path}")

    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
