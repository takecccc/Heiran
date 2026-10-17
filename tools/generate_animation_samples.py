"""Generate animated GIF / APNG / WebP samples that exercise the real encoder paths.

``generate_multiframe_samples.py`` writes a dependency free GIF that never compresses and always
covers the full frame. The files here are written by Pillow, so they carry what real files carry:
LZW compression, a transparent colour, frames that cover only the changed rectangle, and each
disposal method. Everything is drawn by this script, so the files have no third party rights.

* ``bounce-transparent.gif``: a ball on a transparent background, disposal "restore to background".
* ``bounce-optimized.gif``: a ball over a fixed backdrop. Pillow keeps only the changed rectangle of
  each frame (sub-rectangle frames with "do not dispose").
* ``bounce-previous.gif``: the same scene with disposal "restore to previous".
* ``bounce.png`` (APNG), ``bounce.webp``: the same scene with alpha, for the other two formats.

The output goes to ``examples/animation`` and is committed (a few kilobytes each).
"""

from __future__ import annotations

import sys
from pathlib import Path

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parent.parent
DEFAULT_OUTPUT_DIR = ROOT / "examples" / "animation"

SIZE = (160, 120)
FRAME_COUNT = 12
DURATION_MS = 80


def backdrop() -> Image.Image:
    """A fixed gradient with a grid, so a wrong composite shows up as a visible seam."""
    image = Image.new("RGBA", SIZE)
    pixels = image.load()
    for y in range(SIZE[1]):
        for x in range(SIZE[0]):
            base = 40 + (x * 90) // SIZE[0] + (y * 60) // SIZE[1]
            line = x % 20 == 0 or y % 20 == 0
            pixels[x, y] = (base + 40, base, base - 20, 255) if line else (base, base + 20, base + 40, 255)
    return image


def ball_frame(index: int, with_backdrop: bool) -> Image.Image:
    """The ball at its position for ``index``. Transparent around it unless ``with_backdrop``."""
    image = backdrop() if with_backdrop else Image.new("RGBA", SIZE, (0, 0, 0, 0))
    draw = ImageDraw.Draw(image)
    x = 20 + (SIZE[0] - 40) * index // (FRAME_COUNT - 1)
    y = 30 + int(50 * abs(((index * 2) % FRAME_COUNT) / FRAME_COUNT - 0.5) * 2)
    radius = 14
    colour = (230, 70 + index * 12, 60, 255)
    draw.ellipse((x - radius, y - radius, x + radius, y + radius), fill=colour, outline=(255, 255, 255, 255))
    return image


def save_gif(path: Path, frames: list[Image.Image], **options: object) -> None:
    frames[0].save(
        path,
        save_all=True,
        append_images=frames[1:],
        duration=DURATION_MS,
        loop=0,
        **options,
    )


def main() -> None:
    output_dir = Path(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT_OUTPUT_DIR
    output_dir.mkdir(parents=True, exist_ok=True)

    transparent = [ball_frame(i, False) for i in range(FRAME_COUNT)]
    opaque = [ball_frame(i, True) for i in range(FRAME_COUNT)]

    save_gif(output_dir / "bounce-transparent.gif", transparent, disposal=2, transparency=0)
    save_gif(output_dir / "bounce-optimized.gif", opaque, optimize=True, disposal=1)
    save_gif(output_dir / "bounce-previous.gif", opaque, optimize=True, disposal=3)

    transparent[0].save(
        output_dir / "bounce.png",
        save_all=True,
        append_images=transparent[1:],
        duration=DURATION_MS,
        loop=0,
        disposal=1,
    )
    try:
        transparent[0].save(
            output_dir / "bounce.webp",
            save_all=True,
            append_images=transparent[1:],
            duration=DURATION_MS,
            loop=0,
            lossless=True,
        )
    except (OSError, KeyError, ValueError) as error:
        print(f"skipped bounce.webp: {error}")

    for path in sorted(output_dir.iterdir()):
        print(path.relative_to(ROOT), path.stat().st_size)


if __name__ == "__main__":
    main()
