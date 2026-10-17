#include "heiran/domain/DiffMetrics.hpp"

#include <algorithm>
#include <cmath>

namespace heiran::domain {

void DiffMetricsPartial::add(const float *signedDiffs, std::size_t length, double threshold) {
  // 画素ごとの `double` の演算は、まとめて、float の列から求める (分岐が少なく、ベクトル化しやすい形)。
  double localAbs = 0.0;
  double localSigned = 0.0;
  double localSquares = 0.0;
  float localMax = 0.0F;
  std::int64_t localExceeded = 0;
  const float limit = static_cast<float>(threshold);
  for (std::size_t index = 0; index < length; ++index) {
    const float diff = signedDiffs[index];
    const float absDiff = std::abs(diff);
    localMax = std::max(localMax, absDiff);
    localAbs += absDiff;
    localSigned += diff;
    localSquares += static_cast<double>(diff) * diff;
    localExceeded += absDiff > limit ? 1 : 0;
  }
  count += static_cast<std::int64_t>(length);
  sumAbs += localAbs;
  sumSigned += localSigned;
  sumSquares += localSquares;
  maxAbs = std::max(maxAbs, static_cast<double>(localMax));
  exceeded += localExceeded;
}

void DiffMetricsPartial::merge(const DiffMetricsPartial &other) {
  count += other.count;
  sumAbs += other.sumAbs;
  sumSigned += other.sumSigned;
  sumSquares += other.sumSquares;
  maxAbs = std::max(maxAbs, other.maxAbs);
  exceeded += other.exceeded;
}

DiffMetrics DiffMetricsPartial::finish(double peak) const {
  DiffMetrics metrics;
  metrics.pixelCount = count;
  if (count == 0) {
    metrics.psnrIsInfinite = true;
    return metrics;
  }
  const double total = static_cast<double>(count);
  metrics.maxAbsDiff = maxAbs;
  metrics.exceededPixelCount = exceeded;
  metrics.meanAbsDiff = sumAbs / total;
  metrics.meanSignedDiff = sumSigned / total;
  const double meanSquare = sumSquares / total;
  metrics.rmse = std::sqrt(meanSquare);
  if (meanSquare <= 0.0) {
    metrics.psnrIsInfinite = true;
  } else {
    metrics.psnr = 10.0 * std::log10(peak * peak / meanSquare);
  }
  return metrics;
}

DiffMetrics computeDiffMetrics(const float *signedDiffs, std::size_t count, double threshold,
                               double peak) {
  if (signedDiffs == nullptr || count == 0) {
    DiffMetrics metrics;
    metrics.pixelCount = static_cast<std::int64_t>(count);
    metrics.psnrIsInfinite = true;
    return metrics;
  }
  DiffMetricsPartial partial;
  partial.add(signedDiffs, count, threshold);
  return partial.finish(peak);
}

}  // namespace heiran::domain
