#include "heiran/ui/AnalysisSampler.hpp"

#include "heiran/infra/ParallelRows.hpp"

#include <algorithm>
#include <array>

namespace heiran::ui {

float AnalysisSampler::toValueSpace(float sourceValue) const {
  if (valueSpace == AnalysisValueSpace::Source) {
    return sourceValue;
  }
  return decodeShaderTransfer(sourceValue, sourceTransfer, sourceGamma);
}

float AnalysisSampler::sampleRgbaAtDisplayPixel(int x, int y, int rgbaChannel) const {
  if (!isValid()) {
    return 0.0F;
  }

  const QPoint sourcePixel =
      heiran::domain::mapTransformedToSource({x, y}, sourceSize, orientation);
  return toValueSpace(decodedImage.sampleRgba(sourcePixel.x(), sourcePixel.y(), rgbaChannel));
}

double AnalysisSampler::lumaAtDisplayPixel(int x, int y) const {
  if (!isValid()) {
    return 0.0;
  }

  // 座標の写像はチャネルごとではなく画素ごとに 1 回で済ませる。
  // 差分表示は全画素でこれを呼ぶため、3 回に増やすとそのまま 3 倍効く。
  const QPoint sourcePixel =
      heiran::domain::mapTransformedToSource({x, y}, sourceSize, orientation);
  // 重みを掛けるのは空間へ写した後。線形光での輝度は、符号化された値へ重みを
  // 掛けたものを線形へ戻したものとは別になる。
  const double red = toValueSpace(decodedImage.sampleRgba(sourcePixel.x(), sourcePixel.y(), 0));
  const double green = toValueSpace(decodedImage.sampleRgba(sourcePixel.x(), sourcePixel.y(), 1));
  const double blue = toValueSpace(decodedImage.sampleRgba(sourcePixel.x(), sourcePixel.y(), 2));
  return (lumaWeights.red * red + lumaWeights.green * green + lumaWeights.blue * blue) * 255.0;
}

namespace {

// 元画像座標の並びを表示座標の矩形へ写す。回転と反転の写像はここへ集める。
QVector<float> mapSourcePlaneToDisplay(const QVector<float> &sourcePlane,
                                       const QSize &sourcePlaneSize, const QRect &displayRegion,
                                       const QSize &sourceSize,
                                       const heiran::domain::ImageOrientation &orientation,
                                       float scale) {
  const int width = displayRegion.width();
  const int height = displayRegion.height();
  QVector<float> plane(static_cast<qsizetype>(width) * height, 0.0F);

  // 回転も反転も無い場合は表示座標と元画像座標が一致する。写像を通さず行ごとに
  // 写す。普段はこちらの経路を通るため、ここを分けるだけで大きく変わる。
  const bool identityOrientation =
      heiran::domain::normalizeQuarterTurns(orientation.rotationQuarterTurns) == 0 &&
      !orientation.flipX && !orientation.flipY;

  heiran::infra::forEachRowBand(height, [&](int firstRow, int lastRow) {
    for (int y = firstRow; y < lastRow; ++y) {
      float *destinationRow = plane.data() + static_cast<qsizetype>(y) * width;
      if (identityOrientation) {
        const int sourceY = y + displayRegion.top();
        if (sourceY < 0 || sourceY >= sourcePlaneSize.height()) {
          continue;
        }
        const float *sourceRow =
            sourcePlane.constData() + static_cast<qsizetype>(sourceY) * sourcePlaneSize.width();
        for (int x = 0; x < width; ++x) {
          const int sourceX = x + displayRegion.left();
          if (sourceX < 0 || sourceX >= sourcePlaneSize.width()) {
            continue;
          }
          destinationRow[x] = sourceRow[sourceX] * scale;
        }
        continue;
      }

      for (int x = 0; x < width; ++x) {
        const QPoint sourcePixel = heiran::domain::mapTransformedToSource(
            {x + displayRegion.left(), y + displayRegion.top()}, sourceSize, orientation);
        if (sourcePixel.x() < 0 || sourcePixel.y() < 0 ||
            sourcePixel.x() >= sourcePlaneSize.width() ||
            sourcePixel.y() >= sourcePlaneSize.height()) {
          continue;
        }
        destinationRow[x] =
            sourcePlane.at(static_cast<qsizetype>(sourcePixel.y()) * sourcePlaneSize.width() +
                           sourcePixel.x()) *
            scale;
      }
    }
  });
  return plane;
}

// 回転も反転も無く、領域が元の平面の全体なら、並べ替えは要らない (平面がそのまま表示座標の並びになる)。
bool isIdentityFullRegion(const QRect &displayRegion, const QSize &sourcePlaneSize,
                          const heiran::domain::ImageOrientation &orientation) {
  return heiran::domain::normalizeQuarterTurns(orientation.rotationQuarterTurns) == 0 &&
         !orientation.flipX && !orientation.flipY &&
         displayRegion == QRect(QPoint(0, 0), sourcePlaneSize);
}

// 値の空間への写し (線形光への復号など) の表。曲線は、画素ごとに累乗を求めると、4K の 6 つの平面で数百 ms かかる。
// 0.0-1.0 の値は、4096 区間の表から、線形に補う (誤差は、float の精度より小さい)。範囲外 (HDR の 1.0 超え、負) は、曲線を直接求める。
class ValueSpaceTable {
public:
  explicit ValueSpaceTable(const AnalysisSampler &sampler) : sampler_(&sampler) {
    for (int index = 0; index <= kIntervals; ++index) {
      table_[static_cast<size_t>(index)] = sampler.toValueSpace(static_cast<float>(index) / kIntervals);
    }
  }

  float operator()(float value) const {
    if (!(value >= 0.0F && value <= 1.0F)) {
      return sampler_->toValueSpace(value);
    }
    const float position = value * kIntervals;
    const int index = std::min(static_cast<int>(position), kIntervals - 1);
    const float fraction = position - static_cast<float>(index);
    const float low = table_[static_cast<size_t>(index)];
    return low + (table_[static_cast<size_t>(index) + 1] - low) * fraction;
  }

private:
  static constexpr int kIntervals = 4096;
  const AnalysisSampler *sampler_;
  std::array<float, kIntervals + 1> table_{};
};

// 値の空間への写しを、行を帯に分けて並列に行う。
template <typename Convert>
void convertToValueSpaceInParallel(QVector<float> &plane, int rowWidth, const Convert &convert) {
  const int rows = rowWidth > 0 ? static_cast<int>(plane.size() / rowWidth) : 0;
  heiran::infra::forEachRowBand(rows, [&](int firstRow, int lastRow) {
    float *begin = plane.data() + static_cast<qsizetype>(firstRow) * rowWidth;
    float *end = plane.data() + static_cast<qsizetype>(lastRow) * rowWidth;
    for (float *value = begin; value != end; ++value) {
      *value = convert(*value);
    }
  });
}

}  // namespace

QVector<float> AnalysisSampler::buildLumaPlane(const QRect &displayRegion) const {
  if (!isValid() || displayRegion.width() <= 0 || displayRegion.height() <= 0) {
    return {};
  }

  QVector<float> sourcePlane;
  if (valueSpace == AnalysisValueSpace::Source) {
    sourcePlane = decodedImage.buildSourceLumaPlane(lumaWeights);
  } else {
    // 線形光での輝度は、線形へ戻した各チャネルへ重みを掛けたもの。符号化された
    // 値へ重みを掛けてから戻したものとは別になるため、チャネルごとに作る。
    // 3 面を同時には持たない。4K では 1 面でも 64 MiB を超える。
    const double weights[3] = {lumaWeights.red, lumaWeights.green, lumaWeights.blue};
    for (int channel = 0; channel < 3; ++channel) {
      const QVector<float> channelPlane = decodedImage.buildSourceChannelPlane(channel);
      if (channelPlane.isEmpty()) {
        continue;
      }
      if (sourcePlane.isEmpty()) {
        sourcePlane.assign(channelPlane.size(), 0.0F);
      }
      const auto weight = static_cast<float>(weights[channel]);
      const ValueSpaceTable table(*this);
      const float *channelData = channelPlane.constData();
      float *sourceData = sourcePlane.data();
      const int rowWidth = std::max(1, decodedImage.size().width());
      const int rows = static_cast<int>(channelPlane.size() / rowWidth);
      heiran::infra::forEachRowBand(rows, [&](int firstRow, int lastRow) {
        const qsizetype begin = static_cast<qsizetype>(firstRow) * rowWidth;
        const qsizetype end = static_cast<qsizetype>(lastRow) * rowWidth;
        for (qsizetype index = begin; index < end; ++index) {
          sourceData[index] += weight * table(channelData[index]);
        }
      });
    }
  }
  if (sourcePlane.isEmpty()) {
    return {};
  }

  return mapSourcePlaneToDisplay(sourcePlane, decodedImage.size(), displayRegion, sourceSize,
                                 orientation, 255.0F);
}

QVector<float> AnalysisSampler::buildChannelPlane(const QRect &displayRegion,
                                                  int rgbaChannel) const {
  if (!isValid() || displayRegion.width() <= 0 || displayRegion.height() <= 0) {
    return {};
  }

  QVector<float> sourcePlane = decodedImage.buildSourceChannelPlane(rgbaChannel);
  if (sourcePlane.isEmpty()) {
    return {};
  }

  // 空間への写しは表示座標へ並べ替える前に済ませる。切り出した範囲の外も
  // 通ることになるが、1 画素あたりの処理は同じで、分岐が走査の内側へ入らない。
  if (valueSpace != AnalysisValueSpace::Source) {
    const ValueSpaceTable table(*this);
    convertToValueSpaceInParallel(sourcePlane, decodedImage.size().width(),
                                  [&table](float value) { return table(value); });
  }

  if (isIdentityFullRegion(displayRegion, decodedImage.size(), orientation)) {
    return sourcePlane;
  }
  return mapSourcePlaneToDisplay(sourcePlane, decodedImage.size(), displayRegion, sourceSize,
                                 orientation, 1.0F);
}

bool AnalysisSampler::buildRgbPlanes(const QRect &displayRegion,
                                     QVector<float> (&planes)[3]) const {
  if (!isValid() || displayRegion.width() <= 0 || displayRegion.height() <= 0) {
    return false;
  }
  if (!decodedImage.buildSourceRgbPlanes(planes)) {
    return false;
  }
  const QSize planeSize = decodedImage.size();
  const bool identity = isIdentityFullRegion(displayRegion, planeSize, orientation);
  const ValueSpaceTable table(*this);
  for (auto &plane : planes) {
    if (valueSpace != AnalysisValueSpace::Source) {
      convertToValueSpaceInParallel(plane, planeSize.width(),
                                    [&table](float value) { return table(value); });
    }
    if (!identity) {
      plane = mapSourcePlaneToDisplay(plane, planeSize, displayRegion, sourceSize, orientation,
                                      1.0F);
    }
  }
  return true;
}

bool AnalysisSampler::isGrayscale(const QImage &fallback) const {
  switch (decodedImage.sourceColorModel) {
    case heiran::infra::PixelColorModel::Gray:
    case heiran::infra::PixelColorModel::GrayAlpha:
      return true;
    case heiran::infra::PixelColorModel::RGB:
    case heiran::infra::PixelColorModel::RGBA:
      return false;
    default:
      return fallback.isGrayscale();
  }
}

bool AnalysisSampler::hasAlpha(const QImage &fallback) const {
  switch (decodedImage.sourceColorModel) {
    case heiran::infra::PixelColorModel::GrayAlpha:
    case heiran::infra::PixelColorModel::RGBA:
      return true;
    case heiran::infra::PixelColorModel::Gray:
    case heiran::infra::PixelColorModel::RGB:
      return false;
    default:
      return fallback.hasAlphaChannel();
  }
}

}  // namespace heiran::ui
