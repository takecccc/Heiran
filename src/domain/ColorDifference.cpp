#include "heiran/domain/ColorDifference.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace heiran::domain {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kDegrees = kPi / 180.0;

// 立方根。`std::cbrt` は 1 画素に 6 回呼ぶと支配的になるため、近似の初期値から Halley 法で詰める
// (3 次収束。2 回で倍精度にほぼ届く)。`t` は正の値だけ受ける。
inline double fastCbrt(double t) {
  const float seed = static_cast<float>(t);
  std::uint32_t bits;
  std::memcpy(&bits, &seed, sizeof(bits));
  bits = bits / 3 + 0x2A5137A0U;
  float guess;
  std::memcpy(&guess, &bits, sizeof(guess));
  double y = guess;
  for (int step = 0; step < 2; ++step) {
    const double cube = y * y * y;
    y = y * (cube + 2.0 * t) / (2.0 * cube + t);
  }
  return y;
}

// CIE の f()。暗い側は直線で繋ぐ (cbrt の傾きが無限大になるのを避ける)。
inline double labF(double t) {
  constexpr double kEpsilon = 216.0 / 24389.0;
  constexpr double kKappa = 24389.0 / 27.0;
  return t > kEpsilon ? fastCbrt(t) : (kKappa * t + 16.0) / 116.0;
}

inline double hueAngle(double b, double a) {
  if (a == 0.0 && b == 0.0) {
    return 0.0;
  }
  double angle = std::atan2(b, a) / kDegrees;
  if (angle < 0.0) {
    angle += 360.0;
  }
  return angle;
}

}  // namespace

LabConverter::LabConverter(const Matrix3 &m) {
  for (int row = 0; row < 3; ++row) {
    const double white = m[row * 3] + m[row * 3 + 1] + m[row * 3 + 2];
    for (int column = 0; column < 3; ++column) {
      scaled_[row * 3 + column] = m[row * 3 + column] / white;
    }
  }
}

Lab LabConverter::convert(double red, double green, double blue) const {
  const auto &m = scaled_;
  const double fx = labF(m[0] * red + m[1] * green + m[2] * blue);
  const double fy = labF(m[3] * red + m[4] * green + m[5] * blue);
  const double fz = labF(m[6] * red + m[7] * green + m[8] * blue);
  return {116.0 * fy - 16.0, 500.0 * (fx - fy), 200.0 * (fy - fz)};
}

Lab linearRgbToLab(const Matrix3 &m, double red, double green, double blue) {
  return LabConverter(m).convert(red, green, blue);
}

double deltaE2000(const Lab &first, const Lab &second) {
  const double meanL = (first.l + second.l) / 2.0;
  const double chroma1 = std::hypot(first.a, first.b);
  const double chroma2 = std::hypot(second.a, second.b);
  const double meanChroma = (chroma1 + chroma2) / 2.0;
  const double meanChroma7 = std::pow(meanChroma, 7.0);
  const double g = 0.5 * (1.0 - std::sqrt(meanChroma7 / (meanChroma7 + 6103515625.0)));  // 25^7

  const double a1 = (1.0 + g) * first.a;
  const double a2 = (1.0 + g) * second.a;
  const double c1 = std::hypot(a1, first.b);
  const double c2 = std::hypot(a2, second.b);
  const double h1 = hueAngle(first.b, a1);
  const double h2 = hueAngle(second.b, a2);

  const double deltaL = second.l - first.l;
  const double deltaC = c2 - c1;
  double deltaH = 0.0;
  if (c1 * c2 != 0.0) {
    deltaH = h2 - h1;
    if (deltaH > 180.0) {
      deltaH -= 360.0;
    } else if (deltaH < -180.0) {
      deltaH += 360.0;
    }
  }
  const double deltaBigH = 2.0 * std::sqrt(c1 * c2) * std::sin(deltaH * kDegrees / 2.0);

  const double meanC = (c1 + c2) / 2.0;
  double meanH = h1 + h2;
  if (c1 * c2 == 0.0) {
    meanH = h1 + h2;
  } else if (std::abs(h1 - h2) <= 180.0) {
    meanH = (h1 + h2) / 2.0;
  } else {
    meanH = (h1 + h2 + (h1 + h2 < 360.0 ? 360.0 : -360.0)) / 2.0;
  }

  const double t = 1.0 - 0.17 * std::cos((meanH - 30.0) * kDegrees) +
                   0.24 * std::cos(2.0 * meanH * kDegrees) +
                   0.32 * std::cos((3.0 * meanH + 6.0) * kDegrees) -
                   0.20 * std::cos((4.0 * meanH - 63.0) * kDegrees);
  const double deltaTheta = 30.0 * std::exp(-std::pow((meanH - 275.0) / 25.0, 2.0));
  const double meanC7 = std::pow(meanC, 7.0);
  const double rc = 2.0 * std::sqrt(meanC7 / (meanC7 + 6103515625.0));
  const double lShift = (meanL - 50.0) * (meanL - 50.0);
  const double sl = 1.0 + 0.015 * lShift / std::sqrt(20.0 + lShift);
  const double sc = 1.0 + 0.045 * meanC;
  const double sh = 1.0 + 0.015 * meanC * t;
  const double rt = -std::sin(2.0 * deltaTheta * kDegrees) * rc;

  const double termL = deltaL / sl;
  const double termC = deltaC / sc;
  const double termH = deltaBigH / sh;
  return std::sqrt(termL * termL + termC * termC + termH * termH + rt * termC * termH);
}

double deltaE2000Fast(const Lab &first, const Lab &second) {
  // 基準の実装 (`deltaE2000`) と同じ値を、三角関数と累乗を減らして求める。色相は角度ではなく
  // 単位ベクトルで持ち、平均の色相は 2 つの和の向き (短いほうの弧の二等分線) として得る。
  const double chroma1 = std::sqrt(first.a * first.a + first.b * first.b);
  const double chroma2 = std::sqrt(second.a * second.a + second.b * second.b);
  const double meanChromaAb = (chroma1 + chroma2) / 2.0;
  const double m2 = meanChromaAb * meanChromaAb;
  const double m7 = m2 * m2 * m2 * meanChromaAb;
  const double g = 0.5 * (1.0 - std::sqrt(m7 / (m7 + 6103515625.0)));  // 25^7

  const double a1 = (1.0 + g) * first.a;
  const double a2 = (1.0 + g) * second.a;
  const double c1 = std::sqrt(a1 * a1 + first.b * first.b);
  const double c2 = std::sqrt(a2 * a2 + second.b * second.b);

  const double meanL = (first.l + second.l) / 2.0;
  const double deltaL = second.l - first.l;
  const double deltaC = c2 - c1;
  const double meanC = (c1 + c2) / 2.0;
  const double lShift = (meanL - 50.0) * (meanL - 50.0);
  const double sl = 1.0 + 0.015 * lShift / std::sqrt(20.0 + lShift);
  const double sc = 1.0 + 0.045 * meanC;
  const double termL = deltaL / sl;
  const double termC = deltaC / sc;

  // 無彩色が混じると、色相の項は 0 になる (ΔH' が 0)。
  if (c1 * c2 == 0.0) {
    return std::sqrt(termL * termL + termC * termC);
  }

  const double x1 = a1 / c1, y1 = first.b / c1;
  const double x2 = a2 / c2, y2 = second.b / c2;
  const double dot = x1 * x2 + y1 * y2;
  const double cross = x1 * y2 - y1 * x2;
  const double halfSin = std::sqrt(std::max(0.0, (1.0 - dot) / 2.0));  // |sin(Δh / 2)|
  double deltaBigH = 2.0 * std::sqrt(c1 * c2) * (cross < 0.0 ? -halfSin : halfSin);

  double vx = x1 + x2;
  double vy = y1 + y2;
  double length = std::sqrt(vx * vx + vy * vy);
  if (length < 1e-9) {
    // ちょうど反対の色相。基準の実装と同じく、角度の算術平均にする。
    const double h1 = hueAngle(y1, x1);
    const double h2 = hueAngle(y2, x2);
    const double mean = (h1 + h2) / 2.0;
    // 差の符号は、外積では決まらない。基準の実装と同じく、角度の差の符号に従う。
    deltaBigH = 2.0 * std::sqrt(c1 * c2) * (h2 >= h1 ? halfSin : -halfSin);
    vx = std::cos(mean * kDegrees);
    vy = std::sin(mean * kDegrees);
    length = 1.0;
  }
  const double c = vx / length;
  const double s = vy / length;
  double meanH = std::atan2(s, c) / kDegrees;
  if (meanH < 0.0) {
    meanH += 360.0;
  }

  constexpr double kCos30 = 0.86602540378443864676, kSin30 = 0.5;
  const double cos6 = std::cos(6.0 * kDegrees), sin6 = std::sin(6.0 * kDegrees);
  const double cos63 = std::cos(63.0 * kDegrees), sin63 = std::sin(63.0 * kDegrees);
  const double cos2 = c * c - s * s;
  const double sin2 = 2.0 * c * s;
  const double cos3 = cos2 * c - sin2 * s;
  const double sin3 = sin2 * c + cos2 * s;
  const double cos4 = cos2 * cos2 - sin2 * sin2;
  const double sin4 = 2.0 * sin2 * cos2;
  const double t = 1.0 - 0.17 * (c * kCos30 + s * kSin30) + 0.24 * cos2 +
                   0.32 * (cos3 * cos6 - sin3 * sin6) - 0.20 * (cos4 * cos63 + sin4 * sin63);

  const double c2sq = meanC * meanC;
  const double meanC7 = c2sq * c2sq * c2sq * meanC;
  const double rc = 2.0 * std::sqrt(meanC7 / (meanC7 + 6103515625.0));
  double rt = 0.0;
  const double hueOffset = (meanH - 275.0) / 25.0;
  if (std::abs(hueOffset) < 5.0) {  // exp(-25) (約 1e-11) 未満は 0 とみなす
    const double deltaTheta = 30.0 * std::exp(-hueOffset * hueOffset);
    rt = -std::sin(2.0 * deltaTheta * kDegrees) * rc;
  }
  const double sh = 1.0 + 0.015 * meanC * t;
  const double termH = deltaBigH / sh;
  return std::sqrt(termL * termL + termC * termC + termH * termH + rt * termC * termH);
}

void deltaE2000Plane(const Matrix3 &rgbToXyz, const float *baseRed, const float *baseGreen,
                     const float *baseBlue, const float *targetRed, const float *targetGreen,
                     const float *targetBlue, std::size_t count, float *output) {
  const LabConverter converter(rgbToXyz);
  for (std::size_t index = 0; index < count; ++index) {
    if (baseRed[index] == targetRed[index] && baseGreen[index] == targetGreen[index] &&
        baseBlue[index] == targetBlue[index]) {
      output[index] = 0.0F;
      continue;
    }
    const Lab base = converter.convert(baseRed[index], baseGreen[index], baseBlue[index]);
    const Lab target =
        converter.convert(targetRed[index], targetGreen[index], targetBlue[index]);
    output[index] = static_cast<float>(deltaE2000Fast(base, target));
  }
}

void ColorDifferencePartial::add(const float *deltaE, std::size_t length, double threshold) {
  double localSum = 0.0;
  float localMax = 0.0F;
  std::int64_t localExceeded = 0;
  const float limit = static_cast<float>(threshold);
  for (std::size_t index = 0; index < length; ++index) {
    const float value = deltaE[index];
    localSum += value;
    localMax = std::max(localMax, value);
    localExceeded += value > limit ? 1 : 0;
  }
  count += static_cast<std::int64_t>(length);
  sum += localSum;
  maximum = std::max(maximum, static_cast<double>(localMax));
  exceeded += localExceeded;
}

void ColorDifferencePartial::merge(const ColorDifferencePartial &other) {
  count += other.count;
  sum += other.sum;
  maximum = std::max(maximum, other.maximum);
  exceeded += other.exceeded;
}

ColorDifferenceMetrics ColorDifferencePartial::finish() const {
  ColorDifferenceMetrics metrics;
  metrics.pixelCount = count;
  if (count > 0) {
    metrics.maxDeltaE = maximum;
    metrics.meanDeltaE = sum / static_cast<double>(count);
    metrics.exceededPixelCount = exceeded;
  }
  return metrics;
}

ColorDifferenceMetrics computeColorDifferenceMetrics(const float *deltaE, std::size_t count,
                                                     double threshold) {
  ColorDifferenceMetrics metrics;
  metrics.pixelCount = static_cast<std::int64_t>(count);
  if (deltaE == nullptr || count == 0) {
    return metrics;
  }
  double sum = 0.0;
  double largest = 0.0;
  std::int64_t exceeded = 0;
  for (std::size_t index = 0; index < count; ++index) {
    const double value = deltaE[index];
    sum += value;
    largest = std::max(largest, value);
    exceeded += value > threshold ? 1 : 0;
  }
  metrics.maxDeltaE = largest;
  metrics.meanDeltaE = sum / static_cast<double>(count);
  metrics.exceededPixelCount = exceeded;
  return metrics;
}

}  // namespace heiran::domain
