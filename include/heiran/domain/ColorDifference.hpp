#pragma once

#include "heiran/domain/ColorSpace.hpp"

#include <cstddef>
#include <cstdint>

namespace heiran::domain {

// 線形光の RGB (D65) を CIELAB (D65 の白色点) へ写す。値は 1.0 が白。範囲外の値も通す。
struct Lab {
  double l = 0.0;
  double a = 0.0;
  double b = 0.0;
};

// 白 (1, 1, 1) が、ちょうど無彩色の L=100 になるよう、白色点は行列から求める (行の和)。
// 行列を白色点で割った形を持ち、画素ごとの費用を減らす。
class LabConverter {
 public:
  explicit LabConverter(const Matrix3 &rgbToXyz);
  Lab convert(double red, double green, double blue) const;

 private:
  Matrix3 scaled_;
};

Lab linearRgbToLab(const Matrix3 &rgbToXyz, double red, double green, double blue);

// CIEDE2000 (Sharma, Wu, Dalal 2005 の式)。
double deltaE2000(const Lab &first, const Lab &second);

// `deltaE2000` と同じ値を、三角関数と累乗を減らして求める (画素ごとの計算用)。
double deltaE2000Fast(const Lab &first, const Lab &second);

// 2 枚の線形光の RGB の平面から、画素ごとの ΔE2000 を求める (`count` 画素。1 つの帯を、1 スレッドが受け持つ)。
//
// 2 枚の画素が 6 つの値ともに等しいときは、計算せずに 0 を返す (ほとんどの画素が同じ差分では、
// これが支配的に効く)。
void deltaE2000Plane(const Matrix3 &rgbToXyz, const float *baseRed, const float *baseGreen,
                     const float *baseBlue, const float *targetRed, const float *targetGreen,
                     const float *targetBlue, std::size_t count, float *output);

// ΔE の列から求める指標。
struct ColorDifferenceMetrics {
  std::int64_t pixelCount = 0;
  double maxDeltaE = 0.0;
  double meanDeltaE = 0.0;
  // ΔE が `threshold` を超えた画素の数。
  std::int64_t exceededPixelCount = 0;
};

ColorDifferenceMetrics computeColorDifferenceMetrics(const float *deltaE, std::size_t count,
                                                     double threshold);

// ΔE の列の指標を、分けて数えて、足し合わせる (並列に数えるため)。
struct ColorDifferencePartial {
  std::int64_t count = 0;
  double sum = 0.0;
  double maximum = 0.0;
  std::int64_t exceeded = 0;

  void add(const float *deltaE, std::size_t length, double threshold);
  void merge(const ColorDifferencePartial &other);
  ColorDifferenceMetrics finish() const;
};

}  // namespace heiran::domain
