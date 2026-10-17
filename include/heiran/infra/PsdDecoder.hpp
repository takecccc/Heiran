#pragma once

#include "heiran/infra/RawImage.hpp"

namespace heiran::infra {

// Photoshop PSD。レイヤーは読まず、ファイルの末尾にある、合成済みの画像を読む
// (「互換性を最大にする」で保存されたもの。そうでないと、合成済みの画像は空に近い)。
//
// 対応: グレースケール (1) と RGB (3)、8bit / 16bit / 32bit (浮動小数)、無圧縮と RLE (PackBits)。
// RGB で 4 つ以上のチャンネルがあれば、4 つ目をアルファとして持つ。グレースケールは 2 つ目。
// 画像リソースに埋め込みの ICC プロファイルがあれば取り出す。
// CMYK は、RGB へ素朴に畳む (色の管理はしない)。
// 対応しない: Lab、インデックス、ビットマップ、PSB (巨大な形式)、ZIP 圧縮、32bit の CMYK。
RawImage decodePsd(const QByteArray &fileContents);

}  // namespace heiran::infra
