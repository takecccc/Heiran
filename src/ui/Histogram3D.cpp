#include "heiran/ui/Histogram3D.hpp"

#include "heiran/infra/ParallelRows.hpp"

#include <QMutex>
#include <QMutexLocker>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace heiran::ui {

namespace {

constexpr double kPi = 3.14159265358979323846;

}  // namespace

int clampHistogram3DBins(int binsPerAxis) {
  return std::clamp(binsPerAxis, kMinHistogram3DBins, kMaxHistogram3DBins);
}

int histogram3DBinForValue(double value, double axisMaximum, int binsPerAxis) {
  if (binsPerAxis <= 0 || axisMaximum <= 0.0) {
    return 0;
  }
  const int bin = static_cast<int>(std::floor(value / axisMaximum * binsPerAxis));
  return std::clamp(bin, 0, binsPerAxis - 1);
}

int histogram3DIndex(int redBin, int greenBin, int blueBin, int binsPerAxis) {
  return redBin + binsPerAxis * (greenBin + binsPerAxis * blueBin);
}

void histogram3DSplit(int index, int binsPerAxis, int *redBin, int *greenBin, int *blueBin) {
  *redBin = index % binsPerAxis;
  *greenBin = (index / binsPerAxis) % binsPerAxis;
  *blueBin = index / (binsPerAxis * binsPerAxis);
}

double histogram3DBinLow(int bin, double axisMaximum, int binsPerAxis) {
  return axisMaximum * bin / binsPerAxis;
}

double histogram3DBinHigh(int bin, double axisMaximum, int binsPerAxis) {
  return axisMaximum * (bin + 1) / binsPerAxis;
}

QColor histogram3DBinColor(int redBin, int greenBin, int blueBin, double axisMaximum,
                           int binsPerAxis) {
  const auto channel = [&](int bin) {
    const double center = (histogram3DBinLow(bin, axisMaximum, binsPerAxis) +
                           histogram3DBinHigh(bin, axisMaximum, binsPerAxis)) /
                          2.0;
    return static_cast<int>(std::lround(std::clamp(center, 0.0, 1.0) * 255.0));
  };
  return QColor(channel(redBin), channel(greenBin), channel(blueBin));
}

Histogram3D buildHistogram3D(const AnalysisImage &analysisImage, int binsPerAxis,
                             double axisMaximum) {
  Histogram3D histogram;
  histogram.label = analysisImage.label;
  histogram.binsPerAxis = clampHistogram3DBins(binsPerAxis);
  histogram.axisMaximum = axisMaximum > 0.0 ? axisMaximum : 1.0;
  histogram.selectionApplied =
      analysisImage.selectionRect && !analysisImage.selectionRect->isEmpty();
  const int bins = histogram.binsPerAxis;
  const qsizetype binCount = static_cast<qsizetype>(bins) * bins * bins;
  histogram.counts = QVector<int>(binCount, 0);

  const QRect region =
      histogram.selectionApplied ? *analysisImage.selectionRect : analysisImage.image.rect();
  const QRect bounded = region.intersected(analysisImage.image.rect());
  if (bounded.isEmpty()) {
    return histogram;
  }
  const int width = bounded.width();
  const int height = bounded.height();
  const qsizetype pixelCount = static_cast<qsizetype>(width) * height;

  // チャンネルごとに、領域の平面を 1 回だけ作り、ビンの番号 (1 バイト) へ畳む。
  // `buildChannelPlane` は、領域の大きさに関わらず画像全体の平面を作るため、帯ごとに呼ぶと、
  // 帯の数だけ全体を作り直すことになる (以前は、4K で 600 ms 以上かかっていた)。
  // 3 つの平面を同時には持たない (4K で 1 つ 33 MB)。ビンの番号なら、3 つで 24 MB で済む。
  const double axis = histogram.axisMaximum;
  std::vector<std::uint8_t> binIndex[3];
  for (int channel = 0; channel < 3; ++channel) {
    const QVector<float> plane = analysisImage.sampler.buildChannelPlane(bounded, channel);
    if (plane.size() != pixelCount) {
      return histogram;  // 読めなかった。画素は 0。
    }
    binIndex[channel].resize(static_cast<size_t>(pixelCount));
    std::uint8_t *destination = binIndex[channel].data();
    const float *source = plane.constData();
    heiran::infra::forEachRowBand(height, [&](int firstRow, int lastRow) {
      for (qsizetype index = static_cast<qsizetype>(firstRow) * width;
           index < static_cast<qsizetype>(lastRow) * width; ++index) {
        destination[index] =
            static_cast<std::uint8_t>(histogram3DBinForValue(source[index], axis, bins));
      }
    });
  }

  // 数えるのは、帯ごとに自分の表へ。最後に足し合わせる (共有の表への競合を避ける)。
  QMutex mutex;
  heiran::infra::forEachRowBand(height, [&](int firstRow, int lastRow) {
    std::vector<int> local(static_cast<size_t>(binCount), 0);
    const std::uint8_t *red = binIndex[0].data();
    const std::uint8_t *green = binIndex[1].data();
    const std::uint8_t *blue = binIndex[2].data();
    for (qsizetype index = static_cast<qsizetype>(firstRow) * width;
         index < static_cast<qsizetype>(lastRow) * width; ++index) {
      ++local[static_cast<size_t>(histogram3DIndex(red[index], green[index], blue[index], bins))];
    }
    const QMutexLocker locker(&mutex);
    for (qsizetype index = 0; index < binCount; ++index) {
      histogram.counts[index] += local[static_cast<size_t>(index)];
    }
  });
  histogram.pixelCount = pixelCount;

  for (const int count : std::as_const(histogram.counts)) {
    if (count > 0) {
      ++histogram.occupiedBinCount;
      histogram.maxCount = std::max(histogram.maxCount, count);
    }
  }
  return histogram;
}

Histogram3DPoint projectHistogram3DPoint(double red, double green, double blue, double yawDegrees,
                                         double pitchDegrees) {
  // 立方体の中心を原点にする。赤が x、緑が上 (y)、青が奥行き (z)。
  const double x = red - 0.5;
  const double y = green - 0.5;
  const double z = blue - 0.5;

  const double yaw = yawDegrees * kPi / 180.0;
  const double pitch = pitchDegrees * kPi / 180.0;

  // 縦の軸 (緑) まわりに回す。
  const double rotatedX = x * std::cos(yaw) + z * std::sin(yaw);
  const double rotatedZ = -x * std::sin(yaw) + z * std::cos(yaw);
  // 横の軸 (画面の x) まわりに振る。
  const double pitchedY = y * std::cos(pitch) - rotatedZ * std::sin(pitch);
  const double pitchedZ = y * std::sin(pitch) + rotatedZ * std::cos(pitch);

  return {.x = rotatedX, .y = -pitchedY, .depth = pitchedZ};
}

}  // namespace heiran::ui
