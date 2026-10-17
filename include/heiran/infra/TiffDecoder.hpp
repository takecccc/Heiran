#pragma once

#include "heiran/infra/RawImage.hpp"

#include <QString>

namespace heiran::infra {

// libtiff を直接使う TIFF の復号結果。
struct TiffResult {
  RawImage raw;
  // ファイルが持つページ (IFD) の数。
  int pageCount = 1;
  // 適用した向き (1-8)。適用しなかった (指定がない、または 1) なら 1。
  int appliedOrientation = 1;
};

// TIFF の `pageIndex` ページ目を読む。
//
// 画素の値は、ファイルの値のまま返す (アルファを掛けない)。
//
// **そのまま読む形** (精度を保つ): 8bit / 16bit の整数、16bit / 32bit の浮動小数で、グレー、グレー
// + アルファ、 RGB、RGBA (アルファは `ExtraSamples` の最初)。1
// 画素ごとに詰めた形と、チャンネルごとの平面の両方。 ストリップとタイルの両方。「最小が白」のグレー
// (8bit / 16bit) は反転して返す。
//
// **8bit の RGBA へ変換して読む形** (libtiff の変換を使う): パレット、1 / 2 /
// 4bit、YCbCr、CMYK、Lab など。 精度は 8bit。アルファがあれば、libtiff
// の変換が、掛けた値へ直すことがある。
//
// それ以外 (32bit 整数、倍精度、チャンネルが多いもの、など) は失敗を返す。
//
// `applyOrientation` が真なら、ページの `Orientation` タグに従って、回転と反転を適用する。
// 失敗しても例外は出さない (`ok` が偽)。
TiffResult decodeTiff(const QString &filePath, int pageIndex, bool applyOrientation);

// EXIF の向き (1-8) を、画素へ適用する。1 は何もしない。5-8 は縦横が入れ替わる。
// `RawImage` の画素の並びを組み直す。
void applyOrientationToRawImage(RawImage *image, int orientation);

}  // namespace heiran::infra
