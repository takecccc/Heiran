#include "heiran/ui/ScopeBuilder.hpp"

#include "heiran/infra/ParallelRows.hpp"

#include <QMutex>
#include <QMutexLocker>
#include <algorithm>
#include <cmath>

namespace heiran::ui {

namespace {

// 解析対象の範囲。選択範囲があればそこ、無ければ画像全体 (変化グラフと同じ規則)。
QRect scopeRect(const AnalysisImage &analysisImage) {
  const bool selectionApplied =
      analysisImage.selectionRect && !analysisImage.selectionRect->isEmpty();
  return selectionApplied ? *analysisImage.selectionRect : analysisImage.image.rect();
}

QRgb toRgb(double red, double green, double blue) {
  const auto channel = [](double value) {
    return std::clamp(static_cast<int>(std::lround(value * 255.0)), 0, 255);
  };
  return qRgb(channel(red), channel(green), channel(blue));
}

}  // namespace

WaveformPtr buildWaveform(const AnalysisImage &analysisImage) {
  const QRect region = scopeRect(analysisImage);
  if (region.isEmpty() || !analysisImage.sampler.isValid()) {
    return nullptr;
  }
  QVector<float> planes[3];
  if (!analysisImage.sampler.buildRgbPlanes(region, planes)) {
    return nullptr;
  }
  const int width = region.width();
  const int height = region.height();
  const auto &weights = analysisImage.sampler.lumaWeights;

  auto result = std::make_shared<heiran::domain::WaveformCounts>();
  result->allocate(std::min(width, kWaveformMaxColumns), kWaveformLevels, width, height);
  QMutex mutex;
  heiran::infra::forEachRowBand(height, [&](int firstRow, int lastRow) {
    heiran::domain::WaveformCounts partial;
    partial.allocate(result->columns, result->levels, width, height);
    heiran::domain::accumulateWaveformRows(planes[0].constData(), planes[1].constData(),
                                           planes[2].constData(), width, firstRow, lastRow,
                                           weights.red, weights.green, weights.blue, &partial);
    const QMutexLocker locker(&mutex);
    result->merge(partial);
  });
  return result;
}

VectorscopePtr buildVectorscope(const AnalysisImage &analysisImage) {
  const QRect region = scopeRect(analysisImage);
  if (region.isEmpty() || !analysisImage.sampler.isValid()) {
    return nullptr;
  }
  QVector<float> planes[3];
  if (!analysisImage.sampler.buildRgbPlanes(region, planes)) {
    return nullptr;
  }
  const int width = region.width();
  const int height = region.height();
  const auto &weights = analysisImage.sampler.lumaWeights;

  auto result = std::make_shared<heiran::domain::VectorscopeCounts>();
  result->allocate(kVectorscopeSize);
  QMutex mutex;
  heiran::infra::forEachRowBand(height, [&](int firstRow, int lastRow) {
    heiran::domain::VectorscopeCounts partial;
    partial.allocate(kVectorscopeSize);
    heiran::domain::accumulateVectorscopeRows(planes[0].constData(), planes[1].constData(),
                                              planes[2].constData(), width, firstRow, lastRow,
                                              weights.red, weights.green, weights.blue, &partial);
    const QMutexLocker locker(&mutex);
    result->merge(partial);
  });
  return result;
}

QImage renderWaveformImage(const heiran::domain::WaveformCounts &counts, WaveformMode mode,
                           double gain) {
  if (counts.columns <= 0 || counts.levels <= 0) {
    return {};
  }
  const int columns = counts.columns;
  const int levels = counts.levels;
  const bool parade = mode == WaveformMode::Parade;
  QImage image(parade ? columns * 3 : columns, levels, QImage::Format_RGB32);
  image.fill(Qt::black);

  // 濃さ: 列の中で、一様に散らばっていれば、密度 1。1 つの値へ集まるほど、大きい。飽和させて (1 - exp)、明るさを抑える。
  const double samples = std::max(1.0, counts.samplesPerColumn());
  const double densityScale = static_cast<double>(levels) / samples;
  const double strength = 0.35 * std::max(0.01, gain);
  const auto intensityOf = [&](std::uint32_t count) {
    return count == 0 ? 0.0 : 1.0 - std::exp(-strength * static_cast<double>(count) * densityScale);
  };

  for (int level = 0; level < levels; ++level) {
    // 縦は、上が、値の最大。
    const int y = levels - 1 - level;
    auto *line = reinterpret_cast<QRgb *>(image.scanLine(y));
    for (int column = 0; column < columns; ++column) {
      const size_t index = static_cast<size_t>(level) * columns + column;
      if (mode == WaveformMode::Luma) {
        const double v = intensityOf(counts.luma[index]);
        line[column] = toRgb(0.9 * v, 0.95 * v, 0.9 * v);
      } else if (mode == WaveformMode::Rgb) {
        line[column] = toRgb(intensityOf(counts.red[index]), intensityOf(counts.green[index]),
                             intensityOf(counts.blue[index]));
      } else {
        line[column] = toRgb(intensityOf(counts.red[index]), 0.0, 0.0);
        line[columns + column] = toRgb(0.0, intensityOf(counts.green[index]), 0.0);
        const double blueIntensity = intensityOf(counts.blue[index]);
        line[2 * columns + column] = toRgb(0.2 * blueIntensity, 0.4 * blueIntensity, blueIntensity);
      }
    }
  }
  return image;
}

QImage renderVectorscopeImage(const heiran::domain::VectorscopeCounts &counts,
                              const heiran::domain::LumaWeights &weights, double gain) {
  const int size = counts.size;
  if (size <= 0 || counts.total <= 0) {
    return {};
  }
  QImage image(size, size, QImage::Format_RGB32);
  image.fill(Qt::black);

  // 濃さ: 画素が、升目へ一様に散らばっていれば、密度 1。対数で、まばらな升目も見えるようにする。
  const double densityScale = static_cast<double>(size) * size / static_cast<double>(counts.total);
  const double g = std::max(0.01, gain);
  const double denominator = std::log1p(g * 200.0);

  for (int row = 0; row < size; ++row) {
    auto *line = reinterpret_cast<QRgb *>(image.scanLine(row));
    // 升目の中心の色差 (-0.5 から 0.5)。上が +Cr。
    const double cr = 0.5 - (row + 0.5) / size;
    for (int column = 0; column < size; ++column) {
      const std::uint32_t count = counts.counts[static_cast<size_t>(row) * size + column];
      if (count == 0) {
        continue;
      }
      const double cb = (column + 0.5) / size - 0.5;
      // 升目の色: 一定の輝度 (0.65) に、その色差を足して、RGB へ戻す。
      constexpr double kLuma = 0.65;
      const double r = kLuma + 2.0 * (1.0 - weights.red) * cr;
      const double b = kLuma + 2.0 * (1.0 - weights.blue) * cb;
      const double gr = (kLuma - weights.red * r - weights.blue * b) / weights.green;
      const double density = static_cast<double>(count) * densityScale;
      // まばらな升目も、色が分かる下限を持たせる。
      const double v = 0.2 + 0.8 * std::min(1.0, std::log1p(g * density) / denominator);
      line[column] = toRgb(std::clamp(r, 0.0, 1.0) * v, std::clamp(gr, 0.0, 1.0) * v,
                           std::clamp(b, 0.0, 1.0) * v);
    }
  }
  // 光の点が小さすぎて見えないため、隣 (上下左右) へ、弱くにじませる (実機のスコープの、蛍光の広がりに当たる)。
  const QImage source = image;
  const auto spread = [](QRgb existing, QRgb lit) {
    return qRgb(std::max(qRed(existing), qRed(lit) * 55 / 100),
                std::max(qGreen(existing), qGreen(lit) * 55 / 100),
                std::max(qBlue(existing), qBlue(lit) * 55 / 100));
  };
  for (int row = 0; row < size; ++row) {
    for (int column = 0; column < size; ++column) {
      const QRgb lit = source.pixel(column, row);
      if (lit == qRgb(0, 0, 0)) {
        continue;
      }
      const int neighbors[4][2] = {{column - 1, row}, {column + 1, row}, {column, row - 1}, {column, row + 1}};
      for (const auto &neighbor : neighbors) {
        if (neighbor[0] >= 0 && neighbor[0] < size && neighbor[1] >= 0 && neighbor[1] < size) {
          image.setPixel(neighbor[0], neighbor[1], spread(image.pixel(neighbor[0], neighbor[1]), lit));
        }
      }
    }
  }
  return image;
}

}  // namespace heiran::ui
