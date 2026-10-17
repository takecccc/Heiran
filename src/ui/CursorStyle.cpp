#include "heiran/ui/CursorStyle.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace heiran::ui {

namespace {

struct Lab {
  double l = 0.0;
  double a = 0.0;
  double b = 0.0;
};

double linearFromSrgb(int value) {
  const double v = value / 255.0;
  return v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4);
}

double labCurve(double t) {
  constexpr double kEpsilon = 216.0 / 24389.0;
  constexpr double kKappa = 24389.0 / 27.0;
  return t > kEpsilon ? std::cbrt(t) : (kKappa * t + 16.0) / 116.0;
}

Lab labOf(QRgb color) {
  const double r = linearFromSrgb(qRed(color));
  const double g = linearFromSrgb(qGreen(color));
  const double b = linearFromSrgb(qBlue(color));
  // sRGB (D65) から XYZ、D65 の白で割って Lab へ。
  const double x = (0.4124564 * r + 0.3575761 * g + 0.1804375 * b) / 0.95047;
  const double y = 0.2126729 * r + 0.7151522 * g + 0.0721750 * b;
  const double z = (0.0193339 * r + 0.1191920 * g + 0.9503041 * b) / 1.08883;
  const double fx = labCurve(x);
  const double fy = labCurve(y);
  const double fz = labCurve(z);
  return {.l = 116.0 * fy - 16.0, .a = 500.0 * (fx - fy), .b = 200.0 * (fy - fz)};
}

double distance(const Lab &first, const Lab &second) {
  return std::sqrt((first.l - second.l) * (first.l - second.l) +
                   (first.a - second.a) * (first.a - second.a) +
                   (first.b - second.b) * (first.b - second.b));
}

// 候補の色。細い線でも見分けやすいよう、彩度と明るさを高く取る。
constexpr std::array<QRgb, 8> kCandidates = {
    qRgb(255, 255, 255), qRgb(0, 0, 0),     qRgb(255, 32, 32), qRgb(32, 230, 32),
    qRgb(48, 96, 255),   qRgb(255, 230, 0), qRgb(0, 230, 230), qRgb(255, 32, 255)};

}  // namespace

QString cursorStyleToString(CursorStyle style) {
  return style == CursorStyle::Contrast ? QStringLiteral("contrast") : QStringLiteral("outline");
}

CursorStyle cursorStyleFromString(const QString &text) {
  return text.trimmed().toLower() == QStringLiteral("contrast") ? CursorStyle::Contrast
                                                                : CursorStyle::Outline;
}

double colorDifference(QRgb first, QRgb second) {
  return distance(labOf(first), labOf(second));
}

QColor chooseContrastColor(const QVector<QRgb> &samples) {
  if (samples.isEmpty()) {
    return QColor(255, 235, 120);
  }

  // 同じ色を何度も変換しない (一様な領域では、ほとんどが同じ色)。
  QVector<Lab> labs;
  QVector<QRgb> seen;
  for (const QRgb sample : samples) {
    const QRgb opaque = sample | 0xFF000000u;
    if (!seen.contains(opaque)) {
      seen.push_back(opaque);
      labs.push_back(labOf(opaque));
    }
  }

  double bestScore = -1.0;
  QRgb best = kCandidates.front();
  for (const QRgb candidate : kCandidates) {
    const Lab candidateLab = labOf(candidate);
    double nearest = std::numeric_limits<double>::max();
    for (const Lab &lab : labs) {
      nearest = std::min(nearest, distance(candidateLab, lab));
    }
    if (nearest > bestScore) {
      bestScore = nearest;
      best = candidate;
    }
  }
  return QColor::fromRgb(best);
}

}  // namespace heiran::ui
