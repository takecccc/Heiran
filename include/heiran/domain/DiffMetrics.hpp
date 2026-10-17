#pragma once

#include <cstddef>
#include <cstdint>

namespace heiran::domain {

// 2 枚の画像の輝度の差 (target - base) から求める数値指標。
struct DiffMetrics {
  // 色差 (ΔE2000) のとき真。`maxAbsDiff` と `meanAbsDiff` は ΔE の最大と平均になり、
  // 符号付きの平均、RMSE、PSNR は意味を持たない。
  bool colorDifference = false;
  // ワイプのとき真。差ではないため、指標は無い (値は、すべて 0)。
  bool wipe = false;
  // SSIM のとき真。`maxAbsDiff` と `meanAbsDiff` は、1 - SSIM の最大と平均 (差の大きさとして、ほかのモードと同じに扱える)。
  // `pixelCount` と `exceededPixelCount` は、窓がすべて画像の中に入る内側の画素だけを数える。
  bool ssim = false;
  double ssimMean = 1.0;
  double ssimMin = 1.0;
  std::int64_t pixelCount = 0;
  double maxAbsDiff = 0.0;
  double meanAbsDiff = 0.0;
  // 差の平均 (符号付き)。正なら target が明るい。全体の明暗のずれ。
  double meanSignedDiff = 0.0;
  // 二乗平均平方根誤差。
  double rmse = 0.0;
  // ピーク信号対雑音比 (dB)。差が無いとき (rmse が 0) は無限大。
  double psnr = 0.0;
  bool psnrIsInfinite = false;
  // 差の絶対値が閾値を超えた画素の数。
  std::int64_t exceededPixelCount = 0;
};

// 差の列の指標を、分けて数えて、足し合わせる (並列に数えるため)。`computeDiffMetrics` は、これを 1 つだけ使う。
struct DiffMetricsPartial {
  std::int64_t count = 0;
  double sumAbs = 0.0;
  double sumSigned = 0.0;
  double sumSquares = 0.0;
  double maxAbs = 0.0;
  std::int64_t exceeded = 0;

  void add(const float *signedDiffs, std::size_t length, double threshold);
  void merge(const DiffMetricsPartial &other);
  DiffMetrics finish(double peak = 255.0) const;
};

// `signedDiffs` は target - base (輝度、ピークが `peak` になる尺度)。
// 8bit 相当の尺度なら `peak` は 255。空なら、すべて 0 で `psnrIsInfinite` は真。
DiffMetrics computeDiffMetrics(const float *signedDiffs, std::size_t count, double threshold,
                               double peak = 255.0);

}  // namespace heiran::domain
