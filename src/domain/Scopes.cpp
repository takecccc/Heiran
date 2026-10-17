#include "heiran/domain/Scopes.hpp"

#include <algorithm>
#include <cmath>

namespace heiran::domain {

namespace {

// 0.0-1.0 の値を、升目の番号へ。範囲外 (HDR の 1.0 超え、負) は、端の升目へ収める。
inline int binOf(float value, int bins) {
  if (!(value > 0.0F)) {  // NaN も、ここへ入る
    return 0;
  }
  const float scaled = value * static_cast<float>(bins);
  return scaled >= static_cast<float>(bins) ? bins - 1 : static_cast<int>(scaled);
}

}  // namespace

void WaveformCounts::allocate(int columnCount, int levelCount, int width, int height) {
  columns = columnCount;
  levels = levelCount;
  sourceWidth = width;
  sourceHeight = height;
  const size_t size = static_cast<size_t>(columnCount) * static_cast<size_t>(levelCount);
  red.assign(size, 0);
  green.assign(size, 0);
  blue.assign(size, 0);
  luma.assign(size, 0);
}

void WaveformCounts::merge(const WaveformCounts &other) {
  const auto add = [](std::vector<std::uint32_t> &into, const std::vector<std::uint32_t> &from) {
    for (size_t index = 0; index < into.size() && index < from.size(); ++index) {
      into[index] += from[index];
    }
  };
  add(red, other.red);
  add(green, other.green);
  add(blue, other.blue);
  add(luma, other.luma);
}

void accumulateWaveformRows(const float *red, const float *green, const float *blue, int width,
                            int firstRow, int lastRow, double weightRed, double weightGreen,
                            double weightBlue, WaveformCounts *counts) {
  if (counts == nullptr || counts->columns <= 0 || counts->levels <= 0 || width <= 0) {
    return;
  }
  const int columns = counts->columns;
  const int levels = counts->levels;
  const float wr = static_cast<float>(weightRed);
  const float wg = static_cast<float>(weightGreen);
  const float wb = static_cast<float>(weightBlue);

  // 画像の列から、升目の列への対応は、行によらない。1 度だけ求める。
  std::vector<int> columnOf(static_cast<size_t>(width));
  for (int x = 0; x < width; ++x) {
    columnOf[static_cast<size_t>(x)] =
        static_cast<int>(static_cast<std::int64_t>(x) * columns / width);
  }

  std::uint32_t *redCounts = counts->red.data();
  std::uint32_t *greenCounts = counts->green.data();
  std::uint32_t *blueCounts = counts->blue.data();
  std::uint32_t *lumaCounts = counts->luma.data();
  for (int y = firstRow; y < lastRow; ++y) {
    const size_t rowStart = static_cast<size_t>(y) * static_cast<size_t>(width);
    for (int x = 0; x < width; ++x) {
      const float r = red[rowStart + static_cast<size_t>(x)];
      const float g = green[rowStart + static_cast<size_t>(x)];
      const float b = blue[rowStart + static_cast<size_t>(x)];
      const size_t column = static_cast<size_t>(columnOf[static_cast<size_t>(x)]);
      ++redCounts[static_cast<size_t>(binOf(r, levels)) * columns + column];
      ++greenCounts[static_cast<size_t>(binOf(g, levels)) * columns + column];
      ++blueCounts[static_cast<size_t>(binOf(b, levels)) * columns + column];
      ++lumaCounts[static_cast<size_t>(binOf(wr * r + wg * g + wb * b, levels)) * columns + column];
    }
  }
}

void VectorscopeCounts::allocate(int gridSize) {
  size = gridSize;
  total = 0;
  counts.assign(static_cast<size_t>(gridSize) * static_cast<size_t>(gridSize), 0);
}

void VectorscopeCounts::merge(const VectorscopeCounts &other) {
  for (size_t index = 0; index < counts.size() && index < other.counts.size(); ++index) {
    counts[index] += other.counts[index];
  }
  total += other.total;
}

void accumulateVectorscopeRows(const float *red, const float *green, const float *blue, int width,
                               int firstRow, int lastRow, double weightRed, double weightGreen,
                               double weightBlue, VectorscopeCounts *counts) {
  if (counts == nullptr || counts->size <= 0 || width <= 0) {
    return;
  }
  const int size = counts->size;
  const float wr = static_cast<float>(weightRed);
  const float wg = static_cast<float>(weightGreen);
  const float wb = static_cast<float>(weightBlue);
  const float cbScale = static_cast<float>(1.0 / (2.0 * (1.0 - weightBlue)));
  const float crScale = static_cast<float>(1.0 / (2.0 * (1.0 - weightRed)));
  std::uint32_t *grid = counts->counts.data();

  for (int y = firstRow; y < lastRow; ++y) {
    const size_t rowStart = static_cast<size_t>(y) * static_cast<size_t>(width);
    for (int x = 0; x < width; ++x) {
      const float r = red[rowStart + static_cast<size_t>(x)];
      const float g = green[rowStart + static_cast<size_t>(x)];
      const float b = blue[rowStart + static_cast<size_t>(x)];
      const float luma = wr * r + wg * g + wb * b;
      // 0.0-1.0 へ写す (色差の範囲 -0.5 から 0.5 を、0 から 1 へ)。上が +Cr のため、縦は、反転する。
      const float cb = (b - luma) * cbScale + 0.5F;
      const float cr = (r - luma) * crScale + 0.5F;
      const int column = binOf(cb, size);
      const int row = size - 1 - binOf(cr, size);
      ++grid[static_cast<size_t>(row) * static_cast<size_t>(size) + static_cast<size_t>(column)];
    }
  }
  counts->total += static_cast<std::int64_t>(lastRow - firstRow) * width;
}

ChromaPoint chromaOf(double red, double green, double blue, double weightRed, double weightGreen,
                     double weightBlue) {
  const double luma = weightRed * red + weightGreen * green + weightBlue * blue;
  return {.cb = (blue - luma) / (2.0 * (1.0 - weightBlue)),
          .cr = (red - luma) / (2.0 * (1.0 - weightRed))};
}

}  // namespace heiran::domain
