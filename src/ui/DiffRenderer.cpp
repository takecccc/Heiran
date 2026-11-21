#include "heiran/ui/DiffRenderer.hpp"

#include "heiran/domain/ColorDifference.hpp"
#include "heiran/domain/Ssim.hpp"
#include "heiran/infra/ParallelRows.hpp"

#include <QCoreApplication>
#include <QPainter>
#include <QRect>
#include <QVector>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <mutex>
#include <utility>
#include <vector>

namespace heiran::ui {

namespace {

using heiran::domain::DiffComparisonBasis;
using heiran::domain::DiffMode;
using heiran::domain::DiffViewMode;

// 1 つの軸を変える重み。縮めるなら、元の区間と先の区間の重なり (面積) で平均する。
// 広げるなら、画素の中心どうしの線形補間。
struct AxisTap {
  int first = 0;
  QVector<float> weights;
};

QVector<AxisTap> axisTaps(int from, int to) {
  QVector<AxisTap> taps(to);
  const double scale = static_cast<double>(from) / to;
  if (to < from) {
    for (int index = 0; index < to; ++index) {
      const double begin = index * scale;
      const double end = begin + scale;
      const int first = static_cast<int>(std::floor(begin));
      const int last = std::min(from - 1, static_cast<int>(std::ceil(end)) - 1);
      AxisTap tap;
      tap.first = first;
      double total = 0.0;
      for (int source = first; source <= last; ++source) {
        const double overlap =
            std::min(end, source + 1.0) - std::max(begin, static_cast<double>(source));
        tap.weights.push_back(static_cast<float>(overlap));
        total += overlap;
      }
      for (float &weight : tap.weights) {
        weight = static_cast<float>(weight / total);
      }
      taps[index] = tap;
    }
  } else {
    for (int index = 0; index < to; ++index) {
      const double position = (index + 0.5) * scale - 0.5;
      const int low = std::clamp(static_cast<int>(std::floor(position)), 0, from - 1);
      const int high = std::min(low + 1, from - 1);
      const float fraction =
          static_cast<float>(std::clamp(position - std::floor(position), 0.0, 1.0));
      AxisTap tap;
      tap.first = low;
      if (high == low || position < 0.0) {
        tap.weights = {1.0F};
      } else {
        tap.weights = {1.0F - fraction, fraction};
      }
      taps[index] = tap;
    }
  }
  return taps;
}

}  // namespace

QVector<float> resizeLumaPlane(const QVector<float> &plane, QSize from, QSize to) {
  if (from == to || from.isEmpty() || to.isEmpty() ||
      plane.size() != static_cast<qsizetype>(from.width()) * from.height()) {
    return plane;
  }
  // 水平: 行ごとに幅を変える。
  const QVector<AxisTap> horizontal = axisTaps(from.width(), to.width());
  QVector<float> wide(static_cast<qsizetype>(to.width()) * from.height());
  heiran::infra::forEachRowBand(from.height(), [&](int firstRow, int lastRow) {
    for (int y = firstRow; y < lastRow; ++y) {
      const float *source = plane.constData() + static_cast<qsizetype>(y) * from.width();
      float *target = wide.data() + static_cast<qsizetype>(y) * to.width();
      for (int x = 0; x < to.width(); ++x) {
        const AxisTap &tap = horizontal.at(x);
        float sum = 0.0F;
        for (int index = 0; index < tap.weights.size(); ++index) {
          sum += source[tap.first + index] * tap.weights.at(index);
        }
        target[x] = sum;
      }
    }
  });
  // 垂直: 列を保ったまま、行を変える。
  const QVector<AxisTap> vertical = axisTaps(from.height(), to.height());
  QVector<float> result(static_cast<qsizetype>(to.width()) * to.height());
  heiran::infra::forEachRowBand(to.height(), [&](int firstRow, int lastRow) {
    for (int y = firstRow; y < lastRow; ++y) {
      const AxisTap &tap = vertical.at(y);
      float *target = result.data() + static_cast<qsizetype>(y) * to.width();
      for (int x = 0; x < to.width(); ++x) {
        float sum = 0.0F;
        for (int index = 0; index < tap.weights.size(); ++index) {
          sum += wide.at(static_cast<qsizetype>(tap.first + index) * to.width() + x) *
                 tap.weights.at(index);
        }
        target[x] = sum;
      }
    }
  });
  return result;
}

static QImage matchDisplayImage(const QImage &image, const QSize &size, bool overlapOnly) {
  const QImage converted = image.convertToFormat(QImage::Format_RGBA8888);
  if (converted.size() == size) {
    return converted;
  }
  return overlapOnly ? converted.copy(QRect(QPoint(0, 0), size))
                     : converted.scaled(size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
}

// 2 枚を、境目で切って、つなぎ合わせる。基準は、左 / 上の側。
DiffOutput renderWipe(const QImage &baseImage, const QImage &targetImage, const QSize &size,
                      const heiran::domain::DiffSettings &settings) {
  DiffOutput output;
  const bool overlapOnly = settings.sizeMatch == heiran::domain::DiffSizeMatch::Overlap;
  const QImage first = matchDisplayImage(baseImage, size, overlapOnly);
  const QImage second = matchDisplayImage(targetImage, size, overlapOnly);
  if (first.isNull() || second.isNull() || first.size() != size || second.size() != size) {
    output.error =
        QCoreApplication::translate("DiffRenderer", "Base or target image is not available.");
    return output;
  }

  const bool leftRight = settings.wipeOrientation == heiran::domain::WipeOrientation::LeftRight;
  const int extent = leftRight ? size.width() : size.height();
  const int split =
      std::clamp(static_cast<int>(std::lround(settings.wipePosition * extent)), 0, extent);

  QImage result(size, QImage::Format_RGBA8888);
  for (int y = 0; y < size.height(); ++y) {
    uchar *destination = result.scanLine(y);
    const uchar *firstRow = first.constScanLine(y);
    const uchar *secondRow = second.constScanLine(y);
    if (leftRight) {
      const qsizetype leftBytes = static_cast<qsizetype>(split) * 4;
      std::memcpy(destination, firstRow, static_cast<size_t>(leftBytes));
      std::memcpy(destination + leftBytes, secondRow + leftBytes,
                  static_cast<size_t>(static_cast<qsizetype>(size.width()) * 4 - leftBytes));
    } else {
      std::memcpy(destination, y < split ? firstRow : secondRow,
                  static_cast<size_t>(static_cast<qsizetype>(size.width()) * 4));
    }
  }

  output.ok = true;
  output.image = std::move(result);
  output.metrics.wipe = true;
  output.metrics.pixelCount = static_cast<std::int64_t>(size.width()) * size.height();
  output.metrics.psnrIsInfinite = true;
  return output;
}

namespace {

// 大きさが違うときは、設定に従って、1 つの大きさへ合わせる。
QSize comparisonSize(const QImage &base, const QImage &target,
                     heiran::domain::DiffSizeMatch sizeMatch) {
  QSize size = base.size();
  if (base.size() == target.size()) {
    return size;
  }
  switch (sizeMatch) {
    case heiran::domain::DiffSizeMatch::ToBase:
      return base.size();
    case heiran::domain::DiffSizeMatch::ToTarget:
      return target.size();
    case heiran::domain::DiffSizeMatch::ToLarger:
      return static_cast<qint64>(base.width()) * base.height() >=
                     static_cast<qint64>(target.width()) * target.height()
                 ? base.size()
                 : target.size();
    case heiran::domain::DiffSizeMatch::Overlap:
      // 左上をそろえて、重なる部分だけを比べる。
      return QSize(std::min(base.width(), target.width()),
                   std::min(base.height(), target.height()));
  }
  return size;
}

// 行を帯に分けて、帯ごとの部分の結果を作り、足し合わせる (並列)。
template <typename Partial, typename Fill> Partial reduceRows(int height, const Fill &fill) {
  Partial total;
  std::mutex mutex;
  heiran::infra::forEachRowBand(height, [&](int firstRow, int lastRow) {
    Partial partial;
    fill(firstRow, lastRow, &partial);
    const std::lock_guard<std::mutex> lock(mutex);
    total.merge(partial);
  });
  return total;
}

// 指標を、差の場から求める。閾値が変わっても、場は作り直さず、これだけを求め直す。
heiran::domain::DiffMetrics metricsOf(const DiffField &field, double threshold) {
  const int width = field.size.width();
  const int height = field.size.height();
  const float *values = field.values.data();
  heiran::domain::DiffMetrics metrics;
  switch (field.kind) {
    case DiffFieldKind::Luma: {
      const auto partial = reduceRows<heiran::domain::DiffMetricsPartial>(
          height, [&](int firstRow, int lastRow, heiran::domain::DiffMetricsPartial *part) {
            part->add(values + static_cast<size_t>(firstRow) * width,
                      static_cast<size_t>(lastRow - firstRow) * width, threshold);
          });
      return partial.finish();
    }
    case DiffFieldKind::DeltaE: {
      const auto partial = reduceRows<heiran::domain::ColorDifferencePartial>(
          height, [&](int firstRow, int lastRow, heiran::domain::ColorDifferencePartial *part) {
            part->add(values + static_cast<size_t>(firstRow) * width,
                      static_cast<size_t>(lastRow - firstRow) * width, threshold);
          });
      const auto colorMetrics = partial.finish();
      metrics.colorDifference = true;
      metrics.pixelCount = colorMetrics.pixelCount;
      metrics.maxAbsDiff = colorMetrics.maxDeltaE;
      metrics.meanAbsDiff = colorMetrics.meanDeltaE;
      metrics.exceededPixelCount = colorMetrics.exceededPixelCount;
      metrics.psnrIsInfinite = true;
      return metrics;
    }
    case DiffFieldKind::Ssim: {
      // 場は、1 - SSIM。窓がすべて画像の中に入る内側だけを数える (SSIM の定義)。
      const bool hasInterior =
          width >= heiran::domain::kSsimWindow && height >= heiran::domain::kSsimWindow;
      const int firstRow = hasInterior ? heiran::domain::kSsimRadius : 0;
      const int lastRow = hasInterior ? height - heiran::domain::kSsimRadius : height;
      const int firstColumn = hasInterior ? heiran::domain::kSsimRadius : 0;
      const int lastColumn = hasInterior ? width - heiran::domain::kSsimRadius : width;
      struct Partial {
        double sum = 0.0;
        double maxDissimilarity = -1.0;
        std::int64_t below = 0;
        std::int64_t count = 0;
        void merge(const Partial &other) {
          sum += other.sum;
          maxDissimilarity = std::max(maxDissimilarity, other.maxDissimilarity);
          below += other.below;
          count += other.count;
        }
      };
      const double ssimThreshold = threshold;
      const auto partial =
          reduceRows<Partial>(lastRow - firstRow, [&](int firstBand, int lastBand, Partial *part) {
            for (int row = firstRow + firstBand; row < firstRow + lastBand; ++row) {
              const float *line = values + static_cast<size_t>(row) * width;
              for (int column = firstColumn; column < lastColumn; ++column) {
                const double dissimilarity = line[column];
                part->sum += dissimilarity;
                part->maxDissimilarity = std::max(part->maxDissimilarity, dissimilarity);
                part->below += (1.0 - dissimilarity) < ssimThreshold ? 1 : 0;
              }
              part->count += lastColumn - firstColumn;
            }
          });
      metrics.ssim = true;
      metrics.pixelCount = partial.count;
      metrics.exceededPixelCount = partial.below;
      if (partial.count > 0) {
        metrics.ssimMean = 1.0 - partial.sum / static_cast<double>(partial.count);
        metrics.ssimMin = 1.0 - partial.maxDissimilarity;
        metrics.meanAbsDiff = partial.sum / static_cast<double>(partial.count);
        metrics.maxAbsDiff = partial.maxDissimilarity;
      }
      metrics.psnrIsInfinite = true;
      return metrics;
    }
  }
  return metrics;
}

// 画素の色を決める (見せ方と、計算の種類は、型で決めて、画素ごとの分岐をなくす)。
enum class Shade { AbsoluteLike, Signed, Binary };

inline void put(uchar *pixel, int red, int green, int blue) {
  pixel[0] = static_cast<uchar>(red);
  pixel[1] = static_cast<uchar>(green);
  pixel[2] = static_cast<uchar>(blue);
  pixel[3] = 255;
}

inline int blendHalf(int under, int over) {
  return (under + over) / 2;
}

template <Shade kShade, DiffViewMode kView>
void shadeRows(const float *values, int width, int firstRow, int lastRow, float scale,
               float threshold, const uchar *underlay, qsizetype underlayStride, uchar *output,
               qsizetype outputStride) {
  for (int y = firstRow; y < lastRow; ++y) {
    const float *line = values + static_cast<size_t>(y) * width;
    uchar *outputRow = output + static_cast<qsizetype>(y) * outputStride;
    const uchar *underlayRow =
        underlay != nullptr ? underlay + static_cast<qsizetype>(y) * underlayStride : nullptr;
    for (int x = 0; x < width; ++x) {
      const float signedDiff = line[x];
      const float absDiff = std::abs(signedDiff);
      uchar *pixel = outputRow + static_cast<qsizetype>(x) * 4;
      const uchar *under =
          kView == DiffViewMode::Overlay ? underlayRow + static_cast<qsizetype>(x) * 4 : nullptr;
      if constexpr (kShade == Shade::AbsoluteLike) {
        const int value = std::clamp(static_cast<int>(absDiff * scale + 0.5F), 0, 255);
        if constexpr (kView == DiffViewMode::Overlay) {
          put(pixel, blendHalf(under[0], value), blendHalf(under[1], value),
              blendHalf(under[2], value));
        } else {
          put(pixel, value, value, value);
        }
      } else if constexpr (kShade == Shade::Signed) {
        const int magnitude = std::clamp(static_cast<int>(absDiff * scale + 0.5F), 0, 255);
        const int red = signedDiff >= 0.0F ? magnitude : 0;
        const int blue = signedDiff >= 0.0F ? 0 : magnitude;
        if constexpr (kView == DiffViewMode::Overlay) {
          put(pixel, blendHalf(under[0], red), blendHalf(under[1], 0), blendHalf(under[2], blue));
        } else if constexpr (kView == DiffViewMode::SignedColor) {
          put(pixel, red, 0, blue);
        } else {
          put(pixel, magnitude, magnitude, magnitude);
        }
      } else {
        const bool exceeded = absDiff > threshold;
        if constexpr (kView == DiffViewMode::Overlay) {
          if (exceeded) {
            put(pixel, blendHalf(under[0], 255), blendHalf(under[1], 196), blendHalf(under[2], 0));
          } else {
            put(pixel, under[0], under[1], under[2]);
          }
        } else if constexpr (kView == DiffViewMode::SignedColor) {
          if (exceeded) {
            signedDiff >= 0.0F ? put(pixel, 255, 0, 0) : put(pixel, 0, 0, 255);
          } else {
            put(pixel, 0, 0, 0);
          }
        } else {
          exceeded ? put(pixel, 255, 255, 255) : put(pixel, 0, 0, 0);
        }
      }
    }
  }
}

template <Shade kShade>
void shadeRowsFor(DiffViewMode view, const float *values, int width, int firstRow, int lastRow,
                  float scale, float threshold, const uchar *underlay, qsizetype underlayStride,
                  uchar *output, qsizetype outputStride) {
  switch (view) {
    case DiffViewMode::Overlay:
      shadeRows<kShade, DiffViewMode::Overlay>(values, width, firstRow, lastRow, scale, threshold,
                                               underlay, underlayStride, output, outputStride);
      break;
    case DiffViewMode::SignedColor:
      shadeRows<kShade, DiffViewMode::SignedColor>(values, width, firstRow, lastRow, scale,
                                                   threshold, underlay, underlayStride, output,
                                                   outputStride);
      break;
    case DiffViewMode::Grayscale:
      shadeRows<kShade, DiffViewMode::Grayscale>(values, width, firstRow, lastRow, scale, threshold,
                                                 underlay, underlayStride, output, outputStride);
      break;
  }
}

QSize diffCanvasSize(const DiffField &field) {
  if (field.inspection && field.sizeMatch == heiran::domain::DiffSizeMatch::Overlap) {
    return field.inspection->baseSize.expandedTo(field.inspection->targetSize);
  }
  return field.size;
}

int ssimBorder(const DiffField &field) {
  return field.kind == DiffFieldKind::Ssim && field.size.width() >= heiran::domain::kSsimWindow &&
                 field.size.height() >= heiran::domain::kSsimWindow
             ? heiran::domain::kSsimRadius
             : 0;
}

bool marksDifference(const DiffField &field, int x, int y, double threshold, int border) {
  const int width = field.size.width();
  const int height = field.size.height();
  if (x >= width || y >= height) {
    if (!field.inspection) {
      return false;
    }
    const auto covers = [&](const QSize &size) { return x < size.width() && y < size.height(); };
    return covers(field.inspection->baseSize) != covers(field.inspection->targetSize);
  }
  const auto index = static_cast<size_t>(y) * width + x;
  if (field.inspection && !field.inspection->alphaChanged.empty() &&
      field.inspection->alphaChanged[index]) {
    return true;
  }
  if (field.inspection && !field.inspection->bothTransparent.empty() &&
      field.inspection->bothTransparent[index]) {
    return false;
  }
  const float difference = field.values[index];
  if (field.kind == DiffFieldKind::Ssim) {
    return x >= border && y >= border && x < width - border && y < height - border &&
           1.0 - difference < threshold;
  }
  return std::abs(difference) > static_cast<float>(threshold);
}

QImage renderInspection(const DiffField &field, const QImage &base, double threshold,
                        heiran::domain::DiffMetrics &metrics) {
  const auto size = diffCanvasSize(field);
  const bool overlapOnly = field.sizeMatch == heiran::domain::DiffSizeMatch::Overlap;
  const auto &inspection = *field.inspection;
  const bool composited = inspection.comparisonBasis != DiffComparisonBasis::PixelValues;
  QImage result =
      composited ? inspection.compositedBase : matchDisplayImage(base, size, overlapOnly);
  if (result.isNull()) {
    return {};
  }
  if (overlapOnly && !composited) {
    const auto area = [](const QSize &s) { return qint64(s.width()) * s.height(); };
    metrics.pixelCount =
        area(field.inspection->baseSize) + area(field.inspection->targetSize) - area(field.size);
  } else {
    metrics.pixelCount = qint64(size.width()) * size.height();
  }
  uchar *bits = result.bits();
  const qsizetype stride = result.bytesPerLine();
  const int border = ssimBorder(field);
  struct Count {
    qint64 pixels = 0;
    void merge(const Count &other) { pixels += other.pixels; }
  };
  metrics.exceededPixelCount =
      reduceRows<Count>(size.height(), [&](int firstRow, int lastRow, Count *count) {
        for (int y = firstRow; y < lastRow; ++y) {
          uchar *row = bits + static_cast<qsizetype>(y) * stride;
          for (int x = 0; x < size.width(); ++x) {
            if (!marksDifference(field, x, y, threshold, border)) {
              continue;
            }
            uchar *pixel = row + static_cast<qsizetype>(x) * 4;
            if (pixel[3] == 0) {
              put(pixel, 255, 196, 0);
            } else {
              put(pixel, blendHalf(pixel[0], 255), blendHalf(pixel[1], 196),
                  blendHalf(pixel[2], 0));
            }
            ++count->pixels;
          }
        }
      }).pixels;
  return result;
}

}  // namespace

std::array<QImage, 2> prepareDiffBlinkFrames(const QImage &base, const QImage &target,
                                             heiran::domain::DiffSizeMatch sizeMatch,
                                             bool includeOutsideOverlap) {
  if (base.isNull() || target.isNull()) {
    return {};
  }
  const bool overlapOnly = sizeMatch == heiran::domain::DiffSizeMatch::Overlap;
  const QSize size = overlapOnly && includeOutsideOverlap ? base.size().expandedTo(target.size())
                                                          : comparisonSize(base, target, sizeMatch);
  return {matchDisplayImage(base, size, overlapOnly), matchDisplayImage(target, size, overlapOnly)};
}

std::array<QImage, 2> prepareDiffCompositeFrames(const DiffInput &base, const DiffInput &target,
                                                 heiran::domain::DiffSizeMatch sizeMatch,
                                                 DiffComparisonBasis basis) {
  if (base.image.isNull() || target.image.isNull() || basis == DiffComparisonBasis::PixelValues) {
    return {};
  }
  const bool overlapOnly = sizeMatch == heiran::domain::DiffSizeMatch::Overlap;
  const QSize canvas = overlapOnly ? base.image.size().expandedTo(target.image.size())
                                   : comparisonSize(base.image, target.image, sizeMatch);
  const Qt::GlobalColor background =
      basis == DiffComparisonBasis::WhiteBackground ? Qt::white : Qt::black;
  const QColorSpace srgb(QColorSpace::SRgb);
  const auto compose = [&](const DiffInput &input) {
    QImage foreground = input.image;
    const auto &sourceSpace = input.sampler.decodedImage.colorSpace;
    if (sourceSpace.isValid() && foreground.colorSpace() != sourceSpace) {
      foreground.setColorSpace(sourceSpace);
    }
    if (foreground.colorSpace().isValid() && foreground.colorSpace() != srgb) {
      foreground = foreground.convertedToColorSpace(srgb, QImage::Format_RGBA8888);
    }
    if (foreground.isNull()) {
      return QImage();
    }
    QImage result(canvas, QImage::Format_RGBA8888);
    if (result.isNull()) {
      return result;
    }
    result.setColorSpace(srgb);
    result.fill(background);
    QPainter painter(&result);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, !overlapOnly);
    painter.drawImage(QRect(QPoint(), overlapOnly ? foreground.size() : canvas), foreground);
    painter.end();
    return result;
  };
  return {compose(base), compose(target)};
}

QVector<QRect> findDiffRegions(const DiffField &field, double threshold) {
  if (field.size.isEmpty()) {
    return {};
  }
  constexpr int tileSize = 16;
  const auto size = diffCanvasSize(field);
  const int width = size.width();
  const int height = size.height();
  const int columns = (width - 1) / tileSize + 1;
  const int rows = (height - 1) / tileSize + 1;
  QVector<QRect> tiles(static_cast<qsizetype>(columns) * rows);
  QRect *tileRects = tiles.data();
  const int border = ssimBorder(field);
  const int scanBorder = field.inspection ? 0 : border;
  heiran::infra::forEachRowBand(rows, [&](int firstRow, int lastRow) {
    for (int row = firstRow; row < lastRow; ++row) {
      QRect *rects = tileRects + static_cast<qsizetype>(row) * columns;
      const int firstY = std::max(row * tileSize, scanBorder);
      const int lastY = std::min((row + 1) * tileSize, height - scanBorder);
      for (int y = firstY; y < lastY; ++y) {
        for (int x = scanBorder; x < width - scanBorder; ++x) {
          if (marksDifference(field, x, y, threshold, border)) {
            rects[x / tileSize] |= QRect(x, y, 1, 1);
          }
        }
      }
    }
  });

  QVector<QRect> regions;
  QVector<qsizetype> pending;
  for (qsizetype index = 0; index < tiles.size(); ++index) {
    if (tiles.at(index).isEmpty()) {
      continue;
    }
    QRect region;
    const auto visit = [&](qsizetype neighbor) {
      if (!tileRects[neighbor].isEmpty()) {
        region |= std::exchange(tileRects[neighbor], QRect());
        pending.append(neighbor);
      }
    };
    visit(index);
    while (!pending.isEmpty()) {
      const qsizetype tile = pending.takeLast();
      const qsizetype column = tile % columns;
      if (column > 0) {
        visit(tile - 1);
      }
      if (column + 1 < columns) {
        visit(tile + 1);
      }
      if (tile >= columns) {
        visit(tile - columns);
      }
      if (tile + columns < tiles.size()) {
        visit(tile + columns);
      }
    }
    regions.append(region);
  }
  std::sort(regions.begin(), regions.end(), [](const QRect &a, const QRect &b) {
    return a.top() == b.top() ? a.left() < b.left() : a.top() < b.top();
  });
  return regions;
}

DiffFieldKind diffFieldKindOf(DiffMode mode) {
  switch (mode) {
    case DiffMode::DeltaE:
      return DiffFieldKind::DeltaE;
    case DiffMode::Ssim:
      return DiffFieldKind::Ssim;
    default:
      return DiffFieldKind::Luma;
  }
}

bool diffFieldServes(const DiffField &field, const heiran::domain::DiffSettings &settings) {
  return settings.mode != DiffMode::Wipe && field.kind == diffFieldKindOf(settings.mode) &&
         field.sizeMatch == settings.sizeMatch &&
         (!field.inspection ||
          (field.inspection->comparisonBasis == settings.comparisonBasis &&
           field.inspection->ignoreFullyTransparentRgb == settings.ignoreFullyTransparentRgb));
}

DiffFieldPtr computeDiffField(const DiffInput &originalBase, const DiffInput &originalTarget,
                              const heiran::domain::DiffSettings &settings, QString *error,
                              bool inspectCoverageAndAlpha) {
  const auto fail = [&](const QString &message) -> DiffFieldPtr {
    if (error != nullptr) {
      *error = message;
    }
    return nullptr;
  };
  if (originalBase.image.isNull() || originalTarget.image.isNull()) {
    return fail(
        QCoreApplication::translate("DiffRenderer", "Base or target image is not available."));
  }
  const bool composited =
      inspectCoverageAndAlpha && settings.comparisonBasis != DiffComparisonBasis::PixelValues;
  std::array<DiffInput, 2> compositedInputs;
  if (composited) {
    const auto frames = prepareDiffCompositeFrames(originalBase, originalTarget, settings.sizeMatch,
                                                   settings.comparisonBasis);
    if (frames[0].isNull() || frames[1].isNull()) {
      return fail(
          QCoreApplication::translate("DiffRenderer", "Base or target image is not available."));
    }
    for (size_t i = 0; i < frames.size(); ++i) {
      compositedInputs[i] = {
          .image = frames[i],
          .sampler = {.decodedImage = {.displayImage = frames[i]}, .sourceSize = frames[i].size()}};
    }
  }
  const auto &base = composited ? compositedInputs[0] : originalBase;
  const auto &target = composited ? compositedInputs[1] : originalTarget;
  const QSize size = comparisonSize(base.image, target.image, settings.sizeMatch);
  const int width = size.width();
  const int height = size.height();
  // 合わせない (重なる部分だけ) ときは、左上から `size` だけ取る。合わせるときは、元の大きさ全体を
  // 取って、あとで `size` へ変える。
  const bool overlapOnly = settings.sizeMatch == heiran::domain::DiffSizeMatch::Overlap;
  const QSize baseRegion = overlapOnly ? size : base.image.size();
  const QSize targetRegion = overlapOnly ? size : target.image.size();

  auto field = std::make_shared<DiffField>();
  field->kind = diffFieldKindOf(settings.mode);
  field->size = size;
  field->sizeMatch = settings.sizeMatch;
  field->values.resize(static_cast<size_t>(width) * static_cast<size_t>(height));
  const size_t pixelCount = field->values.size();
  const bool ignoreTransparent = !composited && settings.ignoreFullyTransparentRgb;
  QVector<float> baseAlpha;
  QVector<float> targetAlpha;
  if (inspectCoverageAndAlpha) {
    field->inspection =
        DiffInspection{.baseSize = originalBase.image.size(),
                       .targetSize = originalTarget.image.size(),
                       .comparisonBasis = settings.comparisonBasis,
                       .compositedBase = composited ? base.image : QImage(),
                       .ignoreFullyTransparentRgb = settings.ignoreFullyTransparentRgb};
  }
  if (inspectCoverageAndAlpha && !composited) {
    const auto alphaPlane = [](const DiffInput &input, const QSize &region) {
      AnalysisSampler sampler = input.sampler;
      sampler.valueSpace = AnalysisValueSpace::Source;
      return sampler.buildChannelPlane(QRect(input.origin, region), 3);
    };
    baseAlpha = alphaPlane(base, baseRegion);
    targetAlpha = alphaPlane(target, targetRegion);
    const auto leftAlpha = resizeLumaPlane(baseAlpha, baseRegion, size);
    const auto rightAlpha = resizeLumaPlane(targetAlpha, targetRegion, size);
    if (static_cast<size_t>(leftAlpha.size()) != pixelCount ||
        static_cast<size_t>(rightAlpha.size()) != pixelCount) {
      return fail(
          QCoreApplication::translate("DiffRenderer", "Base or target image is not available."));
    }
    if (leftAlpha != rightAlpha) {
      auto &changed = field->inspection->alphaChanged;
      changed.resize(pixelCount);
      const float tolerance = baseRegion == size && targetRegion == size ? 0.0F : 1.0e-6F;
      std::transform(leftAlpha.cbegin(), leftAlpha.cend(), rightAlpha.cbegin(), changed.begin(),
                     [tolerance](float a, float b) {
                       return a != b &&
                              (std::isnan(a) != std::isnan(b) || std::abs(a - b) > tolerance);
                     });
      if (std::ranges::none_of(changed, [](unsigned char value) { return value != 0; })) {
        changed.clear();
      }
    }
    const bool baseHasTransparent = ignoreTransparent && baseAlpha.contains(0.0F);
    const bool targetHasTransparent = ignoreTransparent && targetAlpha.contains(0.0F);
    if (baseHasTransparent && targetHasTransparent) {
      std::vector<unsigned char> transparent(pixelCount);
      std::transform(leftAlpha.cbegin(), leftAlpha.cend(), rightAlpha.cbegin(), transparent.begin(),
                     [](float a, float b) { return a == 0.0F && b == 0.0F; });
      if (std::ranges::any_of(transparent, [](unsigned char value) { return value != 0; })) {
        field->inspection->bothTransparent = std::move(transparent);
      }
    }
    if (!baseHasTransparent) {
      baseAlpha = {};
    }
    if (!targetHasTransparent) {
      targetAlpha = {};
    }
  }

  const auto preparePlane = [&](QVector<float> &plane, const QSize &region,
                                const QVector<float> &alpha) {
    if (!alpha.isEmpty() && plane.size() == alpha.size()) {
      float *values = plane.data();
      std::transform(values, values + plane.size(), alpha.cbegin(), values,
                     [](float value, float opacity) { return opacity == 0.0F ? 0.0F : value; });
    }
    plane = resizeLumaPlane(plane, region, size);
  };

  if (field->kind == DiffFieldKind::DeltaE) {
    // 色差は、RGB の 3 つの平面から。Lab は線形光で測るため、値の空間は線形へ固定する。
    const auto planesOf = [&](const DiffInput &input, const QSize &region,
                              const QVector<float> &alpha, QVector<float>(&planes)[3]) {
      AnalysisSampler sampler = input.sampler;
      sampler.valueSpace = AnalysisValueSpace::Linear;
      if (!sampler.buildRgbPlanes(QRect(input.origin, region), planes)) {
        return false;
      }
      for (auto &plane : planes) {
        preparePlane(plane, region, alpha);
      }
      return true;
    };
    QVector<float> basePlanes[3];
    QVector<float> targetPlanes[3];
    if (!planesOf(base, baseRegion, baseAlpha, basePlanes) ||
        !planesOf(target, targetRegion, targetAlpha, targetPlanes) ||
        static_cast<size_t>(basePlanes[0].size()) != pixelCount ||
        static_cast<size_t>(targetPlanes[0].size()) != pixelCount) {
      return fail(
          QCoreApplication::translate("DiffRenderer", "Base or target image is not available."));
    }
    // 2 枚の元の色空間が違っても、Lab へは基準の色空間の行列で写す
    // (比べる側の原色の違いは、値の違いとして出る)。
    const heiran::domain::Matrix3 matrix = base.sampler.rgbToXyz.value_or(
        heiran::domain::rgbToXyzMatrix(heiran::domain::ColorPrimaries::SRgb));
    heiran::infra::forEachRowBand(height, [&](int firstRow, int lastRow) {
      const size_t offset = static_cast<size_t>(firstRow) * static_cast<size_t>(width);
      heiran::domain::deltaE2000Plane(
          matrix, basePlanes[0].constData() + offset, basePlanes[1].constData() + offset,
          basePlanes[2].constData() + offset, targetPlanes[0].constData() + offset,
          targetPlanes[1].constData() + offset, targetPlanes[2].constData() + offset,
          static_cast<size_t>(lastRow - firstRow) * static_cast<size_t>(width),
          field->values.data() + offset);
    });
    return field;
  }

  // 輝度は画素ごとに取らず、走査の前にまとめて作る。画素ごとに取ると、型と
  // 色モデルの判定を毎回通り、解析用バッファを持たない画像では QImage の
  // 画素取得まで通る。差分そのものより判定の方が重くなる。
  QVector<float> baseLuma = base.sampler.buildLumaPlane(QRect(base.origin, baseRegion));
  QVector<float> targetLuma = target.sampler.buildLumaPlane(QRect(target.origin, targetRegion));
  if (baseLuma.isEmpty() || targetLuma.isEmpty()) {
    return fail(
        QCoreApplication::translate("DiffRenderer", "Base or target image is not available."));
  }
  preparePlane(baseLuma, baseRegion, baseAlpha);
  preparePlane(targetLuma, targetRegion, targetAlpha);
  if (static_cast<size_t>(baseLuma.size()) != pixelCount ||
      static_cast<size_t>(targetLuma.size()) != pixelCount) {
    return fail(
        QCoreApplication::translate("DiffRenderer", "Base or target image is not available."));
  }

  if (field->kind == DiffFieldKind::Ssim) {
    // 構造的類似度。行を帯に分けて並列に求め、見せる値 (1 - SSIM) へ直す。値の幅は、8bit の輝度の
    // 255。
    heiran::infra::forEachRowBand(height, [&](int firstRow, int lastRow) {
      heiran::domain::computeSsimRows(baseLuma.constData(), targetLuma.constData(), width, height,
                                      firstRow, lastRow, 255.0, field->values.data());
      float *begin = field->values.data() + static_cast<size_t>(firstRow) * width;
      float *end = field->values.data() + static_cast<size_t>(lastRow) * width;
      for (float *value = begin; value != end; ++value) {
        *value = 1.0F - *value;
      }
    });
    return field;
  }

  // 輝度の差 (比べる側 - 基準)。帯ごとに、並列に求める。
  const float *baseData = baseLuma.constData();
  const float *targetData = targetLuma.constData();
  float *out = field->values.data();
  heiran::infra::forEachRowBand(height, [&](int firstRow, int lastRow) {
    const size_t begin = static_cast<size_t>(firstRow) * width;
    const size_t end = static_cast<size_t>(lastRow) * width;
    for (size_t index = begin; index < end; ++index) {
      out[index] = targetData[index] - baseData[index];
    }
  });
  return field;
}

DiffOutput renderDiffFromField(const DiffField &field, const DiffInput &base,
                               const heiran::domain::DiffSettings &settings) {
  DiffOutput output;
  const QSize size = field.size;
  const int width = size.width();
  const int height = size.height();
  if (size.isEmpty() || field.values.size() != static_cast<size_t>(width) * height) {
    output.error =
        QCoreApplication::translate("DiffRenderer", "Base or target image is not available.");
    return output;
  }
  const DiffMode diffMode = settings.mode;
  const DiffViewMode viewMode = settings.view;

  output.metrics = metricsOf(field, settings.threshold);
  if (field.inspection) {
    output.image = renderInspection(field, base.image, settings.threshold, output.metrics);
    output.ok = !output.image.isNull();
    if (!output.ok) {
      output.error =
          QCoreApplication::translate("DiffRenderer", "Base or target image is not available.");
    }
    return output;
  }
  const bool colorDifference = field.kind == DiffFieldKind::DeltaE;

  // 自動の範囲合わせを切ったときの尺度。輝度は 0-255 のまま、色差は ΔE 10 を最も明るい階調にする。
  constexpr double kDeltaEFullScale = 10.0;
  const double rangeScale =
      settings.autoRange && output.metrics.maxAbsDiff > 0.0
          ? 255.0 / output.metrics.maxAbsDiff
          : (colorDifference ? 255.0 / kDeltaEFullScale
                             : (field.kind == DiffFieldKind::Ssim ? 255.0 : 1.0));
  const float scale = static_cast<float>(rangeScale * settings.gain);

  // 画素は、すべて書くため、塗りつぶさない。RGBA8888 で作る (表示の側が、そのまま使える形)。
  QImage diffImage(size, QImage::Format_RGBA8888);

  // 重ね合わせのときだけ、基準の画像の色が要る。走査しやすい形 (RGBA8888) へ 1 度だけ変換する。
  QImage underlay;
  if (viewMode == DiffViewMode::Overlay) {
    const bool overlapOnly = settings.sizeMatch == heiran::domain::DiffSizeMatch::Overlap;
    underlay = base.image;
    if (underlay.size() != size) {
      underlay = overlapOnly
                     ? underlay.copy(QRect(QPoint(0, 0), size))
                     : underlay.scaled(size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    }
    if (underlay.format() != QImage::Format_RGBA8888) {
      underlay = underlay.convertToFormat(QImage::Format_RGBA8888);
    }
  }

  // 行の先頭は並列に入る前に確定させる。非 const の scanLine() は内部で
  // detach を通るため、複数のスレッドから呼ぶと安全でない。
  uchar *outputBits = diffImage.bits();
  const qsizetype outputStride = diffImage.bytesPerLine();
  const uchar *underlayBits = underlay.isNull() ? nullptr : underlay.constBits();
  const qsizetype underlayStride = underlay.isNull() ? 0 : underlay.bytesPerLine();
  const float *values = field.values.data();
  const float threshold = static_cast<float>(settings.threshold);

  heiran::infra::forEachRowBand(height, [&](int firstRow, int lastRow) {
    switch (diffMode) {
      case DiffMode::Signed:
        shadeRowsFor<Shade::Signed>(viewMode, values, width, firstRow, lastRow, scale, threshold,
                                    underlayBits, underlayStride, outputBits, outputStride);
        break;
      case DiffMode::Binary:
        shadeRowsFor<Shade::Binary>(viewMode, values, width, firstRow, lastRow, scale, threshold,
                                    underlayBits, underlayStride, outputBits, outputStride);
        break;
      default:
        shadeRowsFor<Shade::AbsoluteLike>(viewMode, values, width, firstRow, lastRow, scale,
                                          threshold, underlayBits, underlayStride, outputBits,
                                          outputStride);
        break;
    }
  });

  output.ok = true;
  output.image = std::move(diffImage);
  return output;
}

DiffOutput renderDiff(const DiffInput &base, const DiffInput &target,
                      const heiran::domain::DiffSettings &settings) {
  if (base.image.isNull() || target.image.isNull()) {
    DiffOutput output;
    output.error =
        QCoreApplication::translate("DiffRenderer", "Base or target image is not available.");
    return output;
  }
  if (settings.mode == DiffMode::Wipe) {
    return renderWipe(base.image, target.image,
                      comparisonSize(base.image, target.image, settings.sizeMatch), settings);
  }
  QString error;
  const DiffFieldPtr field = computeDiffField(base, target, settings, &error);
  if (field == nullptr) {
    DiffOutput output;
    output.error = error;
    return output;
  }
  return renderDiffFromField(*field, base, settings);
}

}  // namespace heiran::ui
