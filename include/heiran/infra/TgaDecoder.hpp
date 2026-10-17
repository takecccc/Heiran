#pragma once

#include "heiran/infra/RawImage.hpp"

namespace heiran::infra {

// Truevision TGA。8bit で展開する。
//
// 対応: パレット (1)、フルカラー (2)、グレー (3) と、それぞれの RLE (9、10、11)。
// 1 画素 8、15、16、24、32 ビット。アルファは、記述子のアルファのビット数が 0 でない場合だけ持つ。
// 原点 (下から上 / 上から下、左から右 / 右から左) は適用済み。
RawImage decodeTga(const QByteArray &fileContents);

}  // namespace heiran::infra
