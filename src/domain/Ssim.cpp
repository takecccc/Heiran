#include "heiran/domain/Ssim.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace heiran::domain {

namespace {

// 11 画素のガウス窓 (σ = 1.5)。合計が 1 になるよう整える。
std::array<float, kSsimWindow> gaussianKernel() {
  std::array<float, kSsimWindow> kernel{};
  double sum = 0.0;
  for (int index = 0; index < kSsimWindow; ++index) {
    const double distance = index - kSsimRadius;
    const double weight = std::exp(-(distance * distance) / (2.0 * 1.5 * 1.5));
    kernel[static_cast<size_t>(index)] = static_cast<float>(weight);
    sum += weight;
  }
  for (auto &value : kernel) {
    value = static_cast<float>(value / sum);
  }
  return kernel;
}

// 1 度に処理する行数。水平のぼかしは、窓の分だけ前後の行も要るため、塊が小さいほど、重なる行が増える。
constexpr int kChunkRows = 64;

}  // namespace

void computeSsimRows(const float *base, const float *target, int width, int height, int firstRow,
                     int lastRow, double dynamicRange, float *output) {
  if (width <= 0 || height <= 0 || firstRow >= lastRow) {
    return;
  }
  static const std::array<float, kSsimWindow> kernel = gaussianKernel();
  const float c1 = static_cast<float>((0.01 * dynamicRange) * (0.01 * dynamicRange));
  const float c2 = static_cast<float>((0.03 * dynamicRange) * (0.03 * dynamicRange));

  const size_t pixels = static_cast<size_t>(width);
  const size_t paddedWidth = pixels + 2 * kSsimRadius;
  // 5 つの量 (x、y、x²、y²、xy) の、水平にぼかした行を、塊の分だけ持つ。
  std::vector<float> blurred;
  std::vector<float> paddedX(paddedWidth);
  std::vector<float> paddedY(paddedWidth);
  std::vector<float> paddedXX(paddedWidth);
  std::vector<float> paddedYY(paddedWidth);
  std::vector<float> paddedXY(paddedWidth);
  std::vector<float> accumulators(5 * pixels);

  for (int chunkStart = firstRow; chunkStart < lastRow; chunkStart += kChunkRows) {
    const int chunkEnd = std::min(lastRow, chunkStart + kChunkRows);
    const int sourceFirst = chunkStart - kSsimRadius;
    const int sourceCount = (chunkEnd - chunkStart) + 2 * kSsimRadius;
    blurred.assign(static_cast<size_t>(sourceCount) * 5 * pixels, 0.0F);

    // 水平のぼかし: 必要な行 (範囲外は、端の行) ごとに、5 つの量をぼかす。
    for (int offset = 0; offset < sourceCount; ++offset) {
      const int row = std::clamp(sourceFirst + offset, 0, height - 1);
      const float *xRow = base + static_cast<size_t>(row) * pixels;
      const float *yRow = target + static_cast<size_t>(row) * pixels;
      for (size_t index = 0; index < paddedWidth; ++index) {
        const int column = std::clamp(static_cast<int>(index) - kSsimRadius, 0, width - 1);
        const float x = xRow[column];
        const float y = yRow[column];
        paddedX[index] = x;
        paddedY[index] = y;
        paddedXX[index] = x * x;
        paddedYY[index] = y * y;
        paddedXY[index] = x * y;
      }
      float *destination = blurred.data() + static_cast<size_t>(offset) * 5 * pixels;
      const std::array<const float *, 5> sources = {paddedX.data(), paddedY.data(), paddedXX.data(),
                                                    paddedYY.data(), paddedXY.data()};
      for (size_t quantity = 0; quantity < 5; ++quantity) {
        float *quantityRow = destination + quantity * pixels;
        for (int tap = 0; tap < kSsimWindow; ++tap) {
          const float weight = kernel[static_cast<size_t>(tap)];
          const float *shifted = sources[quantity] + tap;
          for (size_t column = 0; column < pixels; ++column) {
            quantityRow[column] += weight * shifted[column];
          }
        }
      }
    }

    // 垂直のぼかしと、類似度。
    for (int row = chunkStart; row < chunkEnd; ++row) {
      std::fill(accumulators.begin(), accumulators.end(), 0.0F);
      for (int tap = 0; tap < kSsimWindow; ++tap) {
        const float weight = kernel[static_cast<size_t>(tap)];
        const int offset = (row - chunkStart) + tap;
        const float *source = blurred.data() + static_cast<size_t>(offset) * 5 * pixels;
        for (size_t index = 0; index < 5 * pixels; ++index) {
          accumulators[index] += weight * source[index];
        }
      }
      const float *meanX = accumulators.data();
      const float *meanY = meanX + pixels;
      const float *meanXX = meanY + pixels;
      const float *meanYY = meanXX + pixels;
      const float *meanXY = meanYY + pixels;
      float *destination = output + static_cast<size_t>(row) * pixels;
      for (size_t column = 0; column < pixels; ++column) {
        const float mx = meanX[column];
        const float my = meanY[column];
        const float varianceX = meanXX[column] - mx * mx;
        const float varianceY = meanYY[column] - my * my;
        const float covariance = meanXY[column] - mx * my;
        const float numerator = (2.0F * mx * my + c1) * (2.0F * covariance + c2);
        const float denominator = (mx * mx + my * my + c1) * (varianceX + varianceY + c2);
        destination[column] = numerator / denominator;
      }
    }
  }
}

SsimMetrics computeSsimMetrics(const float *ssim, int width, int height, double threshold) {
  SsimMetrics metrics;
  if (ssim == nullptr || width <= 0 || height <= 0) {
    return metrics;
  }
  const bool hasInterior = width >= kSsimWindow && height >= kSsimWindow;
  const int firstRow = hasInterior ? kSsimRadius : 0;
  const int lastRow = hasInterior ? height - kSsimRadius : height;
  const int firstColumn = hasInterior ? kSsimRadius : 0;
  const int lastColumn = hasInterior ? width - kSsimRadius : width;

  double sum = 0.0;
  double minimum = 2.0;
  std::int64_t below = 0;
  for (int row = firstRow; row < lastRow; ++row) {
    const float *line = ssim + static_cast<size_t>(row) * static_cast<size_t>(width);
    for (int column = firstColumn; column < lastColumn; ++column) {
      const double value = line[column];
      sum += value;
      minimum = std::min(minimum, value);
      below += value < threshold ? 1 : 0;
    }
  }
  metrics.pixelCount = static_cast<std::int64_t>(lastRow - firstRow) * (lastColumn - firstColumn);
  metrics.mean = sum / static_cast<double>(metrics.pixelCount);
  metrics.minimum = minimum;
  metrics.belowThresholdCount = below;
  return metrics;
}

}  // namespace heiran::domain
