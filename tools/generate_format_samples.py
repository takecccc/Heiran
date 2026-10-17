"""AVIF、JPEG XL、PSD の小さな見本を examples/formats へ作る。

テストの入力と、目で確かめるための見本。どれも 4 象限 (赤、緑、青、白) の絵を基本とし、
色を画素で確かめられるようにしてある。

必要なもの: pillow (AVIF の書き出しに対応した版)、pillow-jxl-plugin。
    py -m pip install pillow pillow-jxl-plugin
    py tools/generate_format_samples.py
"""

import struct
from pathlib import Path

from PIL import Image

try:
    import pillow_jxl  # noqa: F401  JPEG XL の書き出しを登録する
except ImportError:  # JPEG XL の見本は作らない
    pillow_jxl = None

OUTPUT = Path(__file__).resolve().parent.parent / "examples" / "formats"
SIZE = 32

RED = (255, 0, 0)
GREEN = (0, 255, 0)
BLUE = (0, 0, 255)
WHITE = (255, 255, 255)


def quadrants(size: int = SIZE, alpha: bool = False) -> Image.Image:
    """左上が赤、右上が緑、左下が青、右下が白。alpha なら、白の象限だけ半透明にする。"""
    mode = "RGBA" if alpha else "RGB"
    image = Image.new(mode, (size, size))
    half = size // 2
    for (x0, y0, color) in (
        (0, 0, RED),
        (half, 0, GREEN),
        (0, half, BLUE),
        (half, half, WHITE),
    ):
        for y in range(y0, y0 + half):
            for x in range(x0, x0 + half):
                if alpha:
                    opacity = 128 if color == WHITE else 255
                    image.putpixel((x, y), color + (opacity,))
                else:
                    image.putpixel((x, y), color)
    return image


def write_psd(path: Path, depth: int) -> None:
    """非圧縮の RGB の PSD (レイヤー無し、合成済みの画像だけ)。depth は 8 か 16。"""
    image = quadrants()
    width, height = image.size
    channels = 3

    header = b"8BPS"
    header += struct.pack(">H", 1)  # 版
    header += b"\x00" * 6
    header += struct.pack(">H", channels)
    header += struct.pack(">II", height, width)
    header += struct.pack(">H", depth)
    header += struct.pack(">H", 3)  # カラーモード: RGB

    body = struct.pack(">I", 0)  # カラーモードのデータ
    body += struct.pack(">I", 0)  # 画像リソース
    body += struct.pack(">I", 0)  # レイヤーとマスク
    body += struct.pack(">H", 0)  # 圧縮: 無し

    # チャンネルごとに、画素を行順に並べる。
    for channel in range(channels):
        for y in range(height):
            for x in range(width):
                value = image.getpixel((x, y))[channel]
                if depth == 16:
                    body += struct.pack(">H", value * 257)
                else:
                    body += struct.pack(">B", value)

    path.write_bytes(header + body)


def main() -> None:
    OUTPUT.mkdir(parents=True, exist_ok=True)

    # AVIF。不可逆のため、色は少しずれる。ずれを抑えるため品質を最大、4:4:4 にする。
    quadrants().save(OUTPUT / "quadrants.avif", quality=100, subsampling="4:4:4")
    quadrants(alpha=True).save(OUTPUT / "quadrants-alpha.avif", quality=100, subsampling="4:4:4")

    # JPEG XL。可逆なので、画素が一致する。
    if pillow_jxl is not None:
        quadrants().save(OUTPUT / "quadrants.jxl", lossless=True)
        quadrants(alpha=True).save(OUTPUT / "quadrants-alpha.jxl", lossless=True)

    # PSD。
    write_psd(OUTPUT / "quadrants-8bit.psd", 8)
    write_psd(OUTPUT / "quadrants-16bit.psd", 16)

    for path in sorted(OUTPUT.iterdir()):
        print(f"{path.name}  {path.stat().st_size} bytes")


if __name__ == "__main__":
    main()
