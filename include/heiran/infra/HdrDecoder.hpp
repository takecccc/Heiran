#pragma once

#include "heiran/infra/RawImage.hpp"

namespace heiran::infra {

// Radiance HDR (RGBE)。浮動小数 (32bit) の RGB へ展開する。値は線形光。
//
// 対応: `#?RADIANCE` / `#?RGBE` で始まるファイル、`FORMAT=32-bit_rle_rgbe`、
// 新しい RLE (行ごとに 4 つの平面)、平坦なもの、古い RLE (`1 1 1 回数`)。
// 解像度の行は `-Y 高さ +X 幅` (標準) と、上下や左右が反転した 3 つの向き。
// 対応しない: XYZE、縦横が入れ替わる向き (`+X ... -Y ...`)。
RawImage decodeRadianceHdr(const QByteArray &fileContents);

}  // namespace heiran::infra
