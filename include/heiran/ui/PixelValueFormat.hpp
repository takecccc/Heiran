#pragma once

#include <QString>

namespace heiran::ui {

// 画素値の表示単位。取得元の精度に関わらず、同じ単位で表示して
// ビット深度の異なる画像を並べても値を直接比較できるようにする。
enum class PixelValueFormat {
  // 0-255 に正規化する。8bit の値は整数で表示する。
  Normalized255,
  // 0.0-1.0 に正規化する。浮動小数画像の値を元のスケールで読める。
  Normalized01,
};

QString pixelValueFormatToString(PixelValueFormat format);

PixelValueFormat pixelValueFormatFromString(const QString &text);

// 正規化値 (整数型は 0.0-1.0、浮動小数型は 1.0 を超えうる) を表示文字列にする。
// 列を揃えるため桁数を固定する。
QString formatPixelSample(float normalizedValue, PixelValueFormat format, bool eightBit = false);

}  // namespace heiran::ui
