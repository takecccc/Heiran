"""別の書き手 (ImageMagick、ffmpeg) で、いろいろな形式のファイルと、期待値を作る。

自分たちの書き手 (テストの中の自作や libtiff / OpenEXR の API) だけで確かめると、読み手と書き手が
同じ思い込みを持っていても気付けない。別の実装が書いたファイルを、元の画像と比べて確かめる。

必要なもの: ImageMagick 7 (`magick`)、ffmpeg (無ければ、その分は飛ばす)。Python の追加パッケージは不要。

    py tools/generate_cross_writer_samples.py [出力先]
    build/heiran_cross_writer_check <出力先>/manifest.tsv

出力先の既定は、一時フォルダの `heiran_cross_writer`。置くのは、元の画像 (src.png、src_a.png)、
各形式のファイル、期待値 (float32 RGBA)、manifest.tsv。リポジトリには入れない。

期待値は、書き出す前の画像から作る。そのため、可逆の形式は許容差 0 (型の丸めを除く) で一致する。
不可逆の形式 (JPEG、AVIF、WebP、JPEG XL、EXR の B44 / DWAA) は、圧縮の誤差を見込んだ許容差を置く。

既知の、書き手の側の癖 (復号の誤りではない) のため、入れていないもの:
  - ImageMagick の TIFF の平面分離 (`-interlace Plane`): 書き出したものが、IM 自身の読み取りとも期待値と合わない。
  - ImageMagick の DPX 12bit: ヘッダーは「詰めない」なのに、中身は詰めた並び。
  - ImageMagick の DPX 10bit のアルファ: 0 で書かれる (アルファ無しにして書く)。
  - ImageMagick の DPX 10bit グレー: 1 語の 3 標本を、仕様と逆の順で書く (IM 自身の読み取りとだけ一致する)。
    RGB の 10bit は、IM と ffmpeg の両方で仕様どおりに読めている。
  - ffmpeg の DPX 16bit RGB (3 チャンネル): IM の読み取りとは一致するが、元の画像とは合わない。
"""

import os
import subprocess
import sys
import tempfile
from pathlib import Path

W, H = 193, 131
FLOAT = ["-define", "quantum:format=floating-point", "-depth", "32"]

out = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(tempfile.gettempdir()) / "heiran_cross_writer"
out.mkdir(parents=True, exist_ok=True)
rows = []


def run(command):
    """成功なら None、失敗なら、その文言。"""
    try:
        result = subprocess.run(command, capture_output=True, text=True)
    except FileNotFoundError:
        return f"{command[0]} is not installed"
    return None if result.returncode == 0 else result.stderr.strip()[:200]


def make_sources():
    # 乱数の絵 (固定の種)。色の端 (黒、白、純色) も入れておく。
    error = run(["magick", "-seed", "7", "-size", f"{W}x{H}", "plasma:fractal", "-depth", "16",
                 str(out / "src.png")])
    if error:
        sys.exit(f"ImageMagick is required: {error}")
    run(["magick", str(out / "src.png"), "-fill", "black", "-draw", "rectangle 0,0 9,9",
         "-fill", "white", "-draw", "rectangle 20,0 29,9", "-fill", "red", "-draw",
         "rectangle 40,0 49,9", "-depth", "16", str(out / "src.png")])
    # アルファ付き。横方向に 10% から 100%。(0 だと、WebP などが、透明な画素の色を捨てるため)
    run(["magick", str(out / "src.png"), "(", "-size", f"{W}x{H}", "gradient:black-white",
         "-rotate", "90", "-resize", f"{W}x{H}!", ")", "-compose",
         "CopyOpacity", "-composite", "-channel", "A", "-evaluate", "Multiply", "0.9",
         "-evaluate", "Add", "10%", "+channel", "-depth", "16", str(out / "src_a.png")])
    # なだらかな絵。色の変換 (YUV など) の誤りを、ノイズに埋もれずに見る。
    run(["magick", "-size", f"{W}x{H}", "gradient:red-blue", "(", "-size", f"{W}x{H}",
         "gradient:black-lime", "-rotate", "90", "-resize", f"{W}x{H}!", ")", "-compose",
         "Screen", "-composite", "-depth", "16", str(out / "src_smooth.png")])


def case(name, source, ops, target, tolerance, ref_ops=None, page=0):
    """`source` へ `ops` をかけて `target` へ書く。期待値は、`ref_ops` (無ければ ops) をかけた画像。"""
    fname = out / target.split(":", 1)[-1]
    prefix = target.split(":", 1)[0] + ":" if ":" in target else ""
    error = run(["magick", str(out / source)] + ops + [prefix + str(fname)])
    if error:
        print(f"SKIP {name}: cannot write: {error}")
        return
    ref = out / (name + ".ref")
    error = run(["magick", str(out / source)] + (ref_ops if ref_ops is not None else ops) +
                ["-alpha", "set", "-depth", "32", "-define", "quantum:format=floating-point",
                 f"rgba:{ref}"])
    if error:
        print(f"SKIP {name}: cannot make the reference: {error}")
        return
    rows.append(f"{name}\t{fname}\t{W}\t{H}\t{tolerance}\t{ref}\t{page}")


def profile_case(name, target, depth, tolerance):
    """CMYK のプロファイル (SWOP) を埋め込んだファイル。期待値は、ImageMagick が同じプロファイルで
    sRGB へ変換したもの (Little CMS どうしだが、使い方 (インテント、黒点補正) が違いうるため、許容差を置く)。
    Windows の標準のプロファイルを使う。無ければ飛ばす。"""
    color = Path(os.environ.get("WINDIR", "C:/Windows")) / "System32/spool/drivers/color"
    cmyk_profile = color / "RSWOP.icm"
    srgb_profile = color / "sRGB Color Space Profile.icm"
    if not (cmyk_profile.exists() and srgb_profile.exists()):
        print(f"SKIP {name}: the Windows colour profiles are not found")
        return
    fname = out / target
    # 元の画像へ sRGB のプロファイルを付け、CMYK のプロファイルへ変換する。
    error = run(["magick", str(out / "src.png"), "-profile", str(srgb_profile), "-profile",
                 str(cmyk_profile), "-depth", str(depth), str(fname)])
    if error:
        print(f"SKIP {name}: cannot write: {error}")
        return
    ref = out / (name + ".ref")
    error = run(["magick", str(fname) + "[0]", "-intent", "Relative", "-black-point-compensation",
                 "-profile", str(srgb_profile), "-alpha", "set", "-depth", "32", "-define",
                 "quantum:format=floating-point", f"rgba:{ref}"])
    if error:
        print(f"SKIP {name}: cannot make the reference: {error}")
        return
    rows.append(f"{name}	{fname}	{W}	{H}	{tolerance}	{ref}	0")


def ffmpeg_case(name, pixel_format, extension, tolerance, gray=False):
    fname = out / f"{name}.{extension}"
    error = run(["ffmpeg", "-y", "-loglevel", "error", "-i", str(out / "src.png"), "-pix_fmt",
                 pixel_format, "-frames:v", "1", str(fname)])
    if error:
        print(f"SKIP {name}: {error}")
        return
    ref = out / (name + ".ref")
    ops = ["-colorspace", "Gray"] if gray else []
    run(["magick", str(out / "src.png")] + ops + ["-alpha", "set", "-depth", "32", "-define",
        "quantum:format=floating-point", f"rgba:{ref}"])
    rows.append(f"{name}\t{fname}\t{W}\t{H}\t{tolerance}\t{ref}\t0")


def main():
    make_sources()
    S, A, M = "src.png", "src_a.png", "src_smooth.png"

    # ---- TIFF
    case("tif_u8", S, ["-depth", "8"], "tif_u8.tif", 0.0)
    case("tif_u16", S, [], "tif_u16.tif", 0.0)
    case("tif_u8_lzw", S, ["-depth", "8", "-compress", "lzw"], "tif_u8_lzw.tif", 0.0)
    case("tif_u16_zip", S, ["-compress", "zip"], "tif_u16_zip.tif", 0.0)
    case("tif_u16_zip_pred", S, ["-compress", "zip", "-define", "tiff:predictor=2"],
         "tif_u16_zip_pred.tif", 0.0)
    case("tif_u16_lzw_pred", S, ["-compress", "lzw", "-define", "tiff:predictor=2"],
         "tif_u16_lzw_pred.tif", 0.0)
    case("tif_u8_tile", S, ["-depth", "8", "-define", "tiff:tile-geometry=64x64"],
         "tif_u8_tile.tif", 0.0)
    case("tif_u16_tile_zip", S, ["-compress", "zip", "-define", "tiff:tile-geometry=48x32"],
         "tif_u16_tile_zip.tif", 0.0)
    case("tif_u8_rps7", S, ["-depth", "8", "-define", "tiff:rows-per-strip=7"],
         "tif_u8_rps7.tif", 0.0)
    case("tif_gray8", S, ["-colorspace", "Gray", "-depth", "8"], "tif_gray8.tif", 0.0)
    case("tif_gray16", S, ["-colorspace", "Gray"], "tif_gray16.tif", 0.0001)
    case("tif_alpha8", A, ["-depth", "8"], "tif_alpha8.tif", 0.0)
    case("tif_alpha16", A, [], "tif_alpha16.tif", 0.0)
    case("tif_f32", S, FLOAT, "tif_f32.tif", 0.0)
    case("tif_f32_alpha", A, FLOAT, "tif_f32_alpha.tif", 0.0)
    case("tif_bigtiff", S, [], "TIFF64:tif_big.tif", 0.0)
    case("tif_jpeg", S, ["-depth", "8", "-compress", "jpeg", "-quality", "95"],
         "tif_jpeg.tif", 0.06)
    case("tif_palette", S, ["-depth", "8", "-colors", "16", "-type", "palette"],
         "tif_palette.tif", 0.01)
    case("tif_mono1", S, ["-monochrome"], "tif_mono1.tif", 0.0)
    case("tif_cmyk", S, ["-depth", "8", "-colorspace", "CMYK"], "tif_cmyk.tif", 0.06,
         ref_ops=["-depth", "8"])
    # 2 ページ目 (階調を反転した絵)
    multi = out / "tif_multi.tif"
    run(["magick", str(out / S), "(", str(out / S), "-negate", ")", str(multi)])
    ref = out / "tif_multi_p1.ref"
    run(["magick", str(out / S), "-negate", "-alpha", "set", "-depth", "32", "-define",
         "quantum:format=floating-point", f"rgba:{ref}"])
    rows.append(f"tif_multi_page2\t{multi}\t{W}\t{H}\t0.0\t{ref}\t1")

    # ---- EXR (IM は half で書く)
    for compression, tolerance in (("none", 0.0005), ("zip", 0.0005), ("piz", 0.0005),
                                   ("rle", 0.0005), ("pxr24", 0.0005), ("b44", 0.15),
                                   ("dwaa", 0.03)):
        case(f"exr_{compression}", S, ["-compress", compression], f"exr_{compression}.exr",
             tolerance)
    case("exr_alpha", A, ["-compress", "zip"], "exr_alpha.exr", 0.0005)
    case("exr_gray", S, ["-colorspace", "Gray", "-compress", "zip"], "exr_gray.exr", 0.0005)

    # ---- PSD
    case("psd_u8", S, ["-depth", "8"], "psd_u8.psd", 0.0)
    case("psd_u16", S, [], "psd_u16.psd", 0.0)
    case("psd_alpha8", A, ["-depth", "8"], "psd_alpha8.psd", 0.0)
    case("psd_alpha16", A, [], "psd_alpha16.psd", 0.0)
    case("psd_gray8", S, ["-colorspace", "Gray", "-depth", "8"], "psd_gray8.psd", 0.0)
    case("psd_gray16", S, ["-colorspace", "Gray"], "psd_gray16.psd", 0.0001)
    case("psd_rle8", S, ["-depth", "8", "-compress", "rle"], "psd_rle8.psd", 0.0)
    # CMYK は、素朴な変換 (K 無し→ K 有りの往復) で元へ戻る。
    case("psd_cmyk8", S, ["-depth", "8", "-colorspace", "CMYK"], "psd_cmyk8.psd", 0.01,
         ref_ops=["-depth", "8"])
    case("psd_cmyk16", S, ["-colorspace", "CMYK"], "psd_cmyk16.psd", 0.001, ref_ops=[])

    # CMYK のプロファイル付き。プロファイルで変換した色と合う (素朴な式とは、色が大きく違う)。
    profile_case("psd_cmyk8_icc", "psd_cmyk8_icc.psd", 8, 0.1)
    profile_case("psd_cmyk16_icc", "psd_cmyk16_icc.psd", 16, 0.1)
    profile_case("tif_cmyk8_icc", "tif_cmyk8_icc.tif", 8, 0.1)
    profile_case("tif_cmyk16_icc", "tif_cmyk16_icc.tif", 16, 0.1)

    # ---- DPX
    case("dpx_8", S, ["-depth", "8"], "dpx_8.dpx", 0.0)
    case("dpx_10", S, ["-alpha", "off", "-depth", "10"], "dpx_10.dpx", 0.002)
    case("dpx_16", S, [], "dpx_16.dpx", 0.0001)

    # ---- HDR (IM は線形で書く)
    case("hdr", S, ["-colorspace", "RGB"], "hdr.hdr", 0.012)

    # ---- TGA
    case("tga24", S, ["-depth", "8"], "tga24.tga", 0.0)
    case("tga32", A, ["-depth", "8"], "tga32.tga", 0.0)
    case("tga_rle", S, ["-depth", "8", "-compress", "rle"], "tga_rle.tga", 0.0)
    case("tga_gray", S, ["-colorspace", "Gray", "-depth", "8"], "tga_gray.tga", 0.0)
    case("tga_palette", S, ["-depth", "8", "-colors", "32", "-type", "palette"],
         "tga_pal.tga", 0.01)

    # ---- AVIF / JPEG XL / WebP / JPEG (不可逆は、なだらかな絵で色の誤りを見る)
    case("avif_smooth", M, ["-depth", "8", "-quality", "95"], "avif_smooth.avif", 0.03)
    case("avif_alpha", A, ["-depth", "8", "-quality", "95"], "avif_alpha.avif", 0.4)
    case("jxl_lossless16", S, ["-quality", "100"], "jxl_ll16.jxl", 0.03)
    case("jxl_alpha", A, ["-depth", "8", "-quality", "100"], "jxl_alpha.jxl", 0.03)
    case("jxl_smooth", M, ["-depth", "8", "-quality", "90"], "jxl_smooth.jxl", 0.12)
    case("webp_lossless", S, ["-depth", "8", "-define", "webp:lossless=true"],
         "webp_ll.webp", 0.0)
    case("webp_alpha_lossless", A, ["-depth", "8", "-define", "webp:lossless=true"],
         "webp_alpha_ll.webp", 0.0)
    case("webp_smooth", M, ["-quality", "92"], "webp_smooth.webp", 0.04)
    case("jpeg_smooth", M, ["-depth", "8", "-quality", "95"], "jpg_smooth.jpg", 0.03)
    case("jpeg_q95", S, ["-depth", "8", "-quality", "95"], "jpg95.jpg", 0.09)
    case("bmp24", S, ["-depth", "8"], "bmp24.bmp", 0.0)

    # ---- PNG
    case("png8", S, ["-depth", "8"], "png8.png", 0.0)
    case("png16", S, [], "png16.png", 0.0)
    case("png_gray16", S, ["-colorspace", "Gray"], "png_gray16.png", 0.0001)
    case("png_alpha16", A, [], "png_alpha16.png", 0.0)
    case("png_palette", S, ["-depth", "8", "-colors", "64", "-type", "palette"],
         "png_pal.png", 0.01)
    case("png_interlaced", S, ["-depth", "8", "-interlace", "PNG"], "png_adam7.png", 0.0)

    # ---- ffmpeg (別の実装)
    ffmpeg_case("ff_dpx_10", "gbrp10le", "dpx", 0.002)
    ffmpeg_case("ff_dpx_rgba64", "rgba64le", "dpx", 0.0002)
    ffmpeg_case("ff_tif_rgb48", "rgb48le", "tiff", 0.0)
    ffmpeg_case("ff_tif_rgba64", "rgba64le", "tiff", 0.0)
    ffmpeg_case("ff_tif_rgb24", "rgb24", "tiff", 0.01)
    ffmpeg_case("ff_tif_gray16", "gray16le", "tiff", 0.15, gray=True)  # 輝度の式が違う
    ffmpeg_case("ff_png_rgb48", "rgb48be", "png", 0.0)

    (out / "manifest.tsv").write_text("\n".join(rows) + "\n", encoding="utf-8")
    print(f"{len(rows)} cases -> {out / 'manifest.tsv'}")


main()
