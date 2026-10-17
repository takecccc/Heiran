#pragma once

#include "heiran/infra/RawImage.hpp"

namespace heiran::infra {

// DPX (SMPTE 268M)。8bit は 8bit、10bit / 12bit / 16bit は 16bit の整数へ展開する
// (10bit と 12bit は、16bit の全範囲へ引き伸ばす)。ログ (Cineon) の符号化も、値のまま読む。
//
// 対応: 要素が RGB (50)、RGBA (51)、ABGR (52)、輝度 (6) の画像。ビッグエンディアンと
// リトルエンディアン。詰め方は 0 (すき間なし。語の下位のビットから詰める)、1 (A: 余りのビットが下)、2 (B: 余りのビットが上)。
// 向きは、左右と上下の反転まで。
// 対応しない: RLE、符号付きの値、要素が分かれた画像 (R、G、B を別の要素に持つもの)、
// 縦横が入れ替わる向き、浮動小数。
RawImage decodeDpx(const QByteArray &fileContents);

}  // namespace heiran::infra
