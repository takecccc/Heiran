#include "heiran/ui/PixelValueFormat.hpp"

namespace heiran::ui {

namespace {

// 16bit の 1 階調は 0-255 換算で 255/65535 = 0.0039 のため、小数 3 桁で分解できる。
constexpr int kNormalized255Decimals = 3;
constexpr int kNormalized255Width = 8;

// 16bit の 1 階調は 0.0-1.0 換算で 1/65535 = 0.0000153 のため、小数 5 桁で分解できる。
constexpr int kNormalized01Decimals = 5;
constexpr int kNormalized01Width = 8;

}  // namespace

QString pixelValueFormatToString(PixelValueFormat format) {
  return format == PixelValueFormat::Normalized01 ? QStringLiteral("normalized01")
                                                  : QStringLiteral("normalized255");
}

PixelValueFormat pixelValueFormatFromString(const QString &text) {
  return text.trimmed().toLower() == QStringLiteral("normalized01")
             ? PixelValueFormat::Normalized01
             : PixelValueFormat::Normalized255;
}

QString formatPixelSample(float normalizedValue, PixelValueFormat format, bool eightBit) {
  if (format == PixelValueFormat::Normalized01) {
    return QString("%1").arg(static_cast<double>(normalizedValue), kNormalized01Width, 'f',
                             kNormalized01Decimals);
  }

  return QString("%1").arg(static_cast<double>(normalizedValue) * 255.0,
                           eightBit ? 3 : kNormalized255Width, 'f',
                           eightBit ? 0 : kNormalized255Decimals);
}

}  // namespace heiran::ui
