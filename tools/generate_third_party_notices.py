"""依存ライブラリのライセンス表記 (THIRD_PARTY_NOTICES.txt) を作る。

vcpkg で入れたライブラリの `share/<port>/copyright` と、Qt、Lucide、マニュアル (HTML) のテーマの表記をまとめる。
配布物 (インストーラ、ZIP) とアプリの About 画面が、このファイルを使う。

    py tools/generate_third_party_notices.py            # THIRD_PARTY_NOTICES.txt を書く
    py tools/generate_third_party_notices.py --check    # 書き出す内容と、今のファイルが同じか確かめる

依存を足す、外す、版を上げたときに流して、結果をコミットする。
vcpkg の installed (既定: vcpkg_installed/x64-windows) が要る。見つからない、または
このスクリプトが知らないライブラリが入っているときは、失敗して知らせる (表記の漏れを防ぐ)。
"""

import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

# vcpkg のライブラリごとの、名前、SPDX の識別子、公式の場所。texts は copyright の中身をそのまま使う。
# `shipped` に入らない (ビルドの道具だけの) ポートは、ここに書かない。
PORTS = {
    "brotli": ("Brotli", "MIT", "https://github.com/google/brotli"),
    "dav1d": ("dav1d", "BSD-2-Clause", "https://code.videolan.org/videolan/dav1d"),
    "highway": ("Highway", "Apache-2.0 OR BSD-3-Clause", "https://github.com/google/highway"),
    "imath": ("Imath", "BSD-3-Clause", "https://github.com/AcademySoftwareFoundation/Imath"),
    "lcms": ("Little CMS (lcms2)", "MIT", "https://www.littlecms.com/"),
    "libavif": ("libavif", "BSD-2-Clause", "https://github.com/AOMediaCodec/libavif"),
    "libdeflate": ("libdeflate", "MIT", "https://github.com/ebiggers/libdeflate"),
    "libjpeg-turbo": ("libjpeg-turbo", "BSD-3-Clause AND IJG AND Zlib", "https://libjpeg-turbo.org/"),
    "libjxl": ("libjxl (JPEG XL)", "BSD-3-Clause", "https://github.com/libjxl/libjxl"),
    "liblzma": ("liblzma (XZ Utils)", "0BSD AND public domain", "https://tukaani.org/xz/"),
    "libspng": ("libspng", "BSD-2-Clause", "https://github.com/randy408/libspng"),
    "libwebp": ("libwebp", "BSD-3-Clause", "https://chromium.googlesource.com/webm/libwebp"),
    "libyuv": ("libyuv", "BSD-3-Clause", "https://chromium.googlesource.com/libyuv/libyuv"),
    "openexr": ("OpenEXR", "BSD-3-Clause", "https://github.com/AcademySoftwareFoundation/openexr"),
    "openjph": ("OpenJPH", "BSD-2-Clause", "https://github.com/aous72/OpenJPH"),
    "tiff": ("libtiff", "libtiff (HPND-sell-variant)", "http://www.libtiff.org/"),
    "zlib": ("zlib", "Zlib", "https://zlib.net/"),
    "zstd": ("Zstandard (zstd)", "BSD-3-Clause (dual-licensed with GPL-2.0-only; used under BSD-3-Clause)",
             "https://github.com/facebook/zstd"),
}

QT_MODULES = "Core, Gui, Widgets, OpenGL, OpenGLWidgets, Svg, Concurrent, PrintSupport"

RULE = "=" * 78


def read_text(path: Path) -> str:
    return path.read_text(encoding="utf-8", errors="replace").strip("\n") + "\n"


def qt_version(build: Path) -> str:
    cache = build / "CMakeCache.txt"
    if cache.exists():
        match = re.search(r"^Qt6_DIR:PATH=.*?/(\d+\.\d+\.\d+)/", cache.read_text(encoding="utf-8", errors="replace"),
                          re.MULTILINE)
        if match:
            return match.group(1)
    return "6.x"


def shipped_ports(installed: Path) -> dict:
    """installed (…/x64-windows) から、実行時に使う (ライブラリを持つ) ポートと版を集める。"""
    info = installed.parent / "vcpkg" / "info"
    triplet = installed.name
    if not info.is_dir():
        sys.exit(f"vcpkg の情報が見つかりません: {info}")
    ports = {}
    for listing in sorted(info.glob(f"*_{triplet}.list")):
        name, version, _ = listing.name.rsplit("_", 2)
        if name.startswith("vcpkg-"):
            continue
        files = listing.read_text(encoding="utf-8", errors="replace").splitlines()
        if any(line.startswith((f"{triplet}/lib/", f"{triplet}/bin/")) and not line.endswith("/") for line in files):
            ports[name] = version
    return ports


def build(installed: Path, build_dir: Path) -> str:
    ports = shipped_ports(installed)
    unknown = sorted(set(ports) - set(PORTS))
    if unknown:
        sys.exit("このスクリプトが知らないライブラリがあります。PORTS へ名前、ライセンス、場所を足してください: "
                 + ", ".join(unknown))

    lines = []
    out = lines.append
    out("Heiran third-party notices\n")
    out("Heiran itself is licensed under the Apache License, Version 2.0 (see LICENSE and NOTICE).")
    out("Heiran uses the software below. Each component is distributed under its own license,")
    out("reproduced in full in the sections that follow.\n")

    entries = [("Qt", qt_version(build_dir), f"LGPL-3.0-only (modules: {QT_MODULES})", "https://www.qt.io/")]
    entries += [(PORTS[name][0], ports[name], PORTS[name][1], PORTS[name][2]) for name in sorted(ports, key=str.lower)]
    entries += [("Lucide icons", "-", "ISC", "https://lucide.dev"),
                ("Manual (HTML) theme and scripts: Furo", "2025.12", "MIT", "https://github.com/pradyunsg/furo"),
                ("Manual (HTML) scripts and styles: Sphinx", "9.1", "BSD-2-Clause (see the text)",
                 "https://www.sphinx-doc.org/"),
                ("Manual (HTML) code styles: Pygments", "2.21", "BSD-2-Clause", "https://pygments.org/")]
    out("Summary")
    out("-------")
    for name, version, spdx, url in entries:
        out(f"  {name} {version}".rstrip() + f"\n      License: {spdx}\n      {url}")
    out("")

    # ---- Qt
    out(RULE)
    out(f"Qt {qt_version(build_dir)}  --  GNU Lesser General Public License, version 3")
    out(RULE)
    out(f"""Heiran uses the Qt libraries ({QT_MODULES}), which are copyright (C) The Qt Company Ltd.
and other contributors, under the GNU Lesser General Public License version 3 (LGPL-3.0).
The full text of the LGPL-3.0, and of the GNU GPL-3.0 that it refers to, follows this section.

Heiran links to Qt dynamically. The Qt libraries are shipped unmodified as separate DLL files
(Qt6*.dll and the plugin folders) next to heiran.exe. You may replace them with another
compatible build of Qt by replacing those files in the installation folder; Heiran does not
prevent this.

The source code of Qt is available from the Qt Project: https://download.qt.io/ and
https://code.qt.io/. The version used by this build of Heiran is Qt {qt_version(build_dir)}.
Information about Qt's licensing: https://www.qt.io/licensing/

The Qt libraries contain third-party code under their own licenses (for example libpng,
FreeType, HarfBuzz, PCRE2, zlib and double-conversion). See
https://doc.qt.io/qt-6/licenses-used-in-qt.html for the list and the notices.
""")

    # ---- vcpkg のライブラリ
    for name in sorted(ports, key=str.lower):
        title, spdx, url = PORTS[name]
        copyright_file = installed / "share" / name / "copyright"
        if not copyright_file.exists():
            sys.exit(f"copyright が見つかりません: {copyright_file}")
        out(RULE)
        out(f"{title} {ports[name]}  --  {spdx}")
        out(url)
        out(RULE)
        out(read_text(copyright_file))

    # ---- Lucide
    out(RULE)
    out("Lucide icons  --  ISC")
    out("https://lucide.dev")
    out(RULE)
    out(read_text(ROOT / "resources" / "icons" / "LICENSE-lucide.txt"))

    # ---- マニュアル (HTML) に含まれる、テーマとスクリプト
    for title, url, license_file in (("Furo (manual theme)  --  MIT", "https://github.com/pradyunsg/furo", "Furo-MIT.txt"),
                                     ("Sphinx (manual scripts and styles)  --  BSD-2-Clause",
                                      "https://www.sphinx-doc.org/", "Sphinx.txt"),
                                     ("Pygments (manual code styles)  --  BSD-2-Clause", "https://pygments.org/",
                                      "Pygments-BSD.txt")):
        out(RULE)
        out(title)
        out(url)
        out(RULE)
        out(read_text(ROOT / "resources" / "licenses" / license_file))

    # ---- ライセンスの全文
    out(RULE)
    out("GNU Lesser General Public License, version 3 (LGPL-3.0)")
    out(RULE)
    out(read_text(ROOT / "resources" / "licenses" / "LGPL-3.0.txt"))
    out(RULE)
    out("GNU General Public License, version 3 (GPL-3.0)")
    out(RULE)
    out(read_text(ROOT / "resources" / "licenses" / "GPL-3.0.txt"))

    return "\n".join(lines).replace("\r\n", "\n")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--installed", type=Path, default=ROOT / "vcpkg_installed" / "x64-windows")
    parser.add_argument("--build-dir", type=Path, default=ROOT / "build")
    parser.add_argument("--output", type=Path, default=ROOT / "THIRD_PARTY_NOTICES.txt")
    parser.add_argument("--check", action="store_true", help="書き出さず、今のファイルと同じかだけ確かめる")
    args = parser.parse_args()

    text = build(args.installed, args.build_dir)
    if args.check:
        current = args.output.read_text(encoding="utf-8") if args.output.exists() else ""
        if current != text:
            sys.exit(f"{args.output.name} が古い。py tools/generate_third_party_notices.py で作り直す")
        print(f"{args.output.name} は最新")
        return
    args.output.write_text(text, encoding="utf-8", newline="\n")
    print(f"{args.output} ({len(text) // 1024} KiB)")


main()
