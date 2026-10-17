#include "heiran/ui/ChangeGraphSeriesBuilder.hpp"

#include <algorithm>

namespace heiran::ui {

namespace {

// 解析対象の範囲を返す。選択範囲があればそこ、無ければ画像全体。
QRect analysisRect(const AnalysisImage &analysisImage) {
  const bool selectionApplied =
      analysisImage.selectionRect && !analysisImage.selectionRect->isEmpty();
  return selectionApplied ? *analysisImage.selectionRect : analysisImage.image.rect();
}

// 行優先で並んだ平面を、指定した向きへ畳んで平均にする。
QVector<float> foldToMeans(const QVector<float> &plane, int width, int height,
                           ChangeGraphDirection direction) {
  if (plane.isEmpty() || width <= 0 || height <= 0) {
    return {};
  }

  const int pointCount = direction == ChangeGraphDirection::Horizontal ? width : height;
  const int sampleCount = direction == ChangeGraphDirection::Horizontal ? height : width;

  // 和は double で持つ。4K の 1 列でも float のままだと桁落ちが見える。
  QVector<double> sums(pointCount, 0.0);
  const float *values = plane.constData();
  for (int y = 0; y < height; ++y) {
    const float *row = values + static_cast<qsizetype>(y) * width;
    if (direction == ChangeGraphDirection::Horizontal) {
      for (int x = 0; x < width; ++x) {
        sums[x] += row[x];
      }
      continue;
    }

    double rowSum = 0.0;
    for (int x = 0; x < width; ++x) {
      rowSum += row[x];
    }
    sums[y] = rowSum;
  }

  QVector<float> means(pointCount, 0.0F);
  for (int index = 0; index < pointCount; ++index) {
    means[index] = static_cast<float>(sums.at(index) / sampleCount);
  }
  return means;
}

float maximumOf(const QVector<float> &values) {
  float maximum = 0.0F;
  for (const float value : values) {
    maximum = std::max(maximum, value);
  }
  return maximum;
}

}  // namespace

float ChangeGraphSeries::maximumMean() const {
  return std::max(
      {maximumOf(redMeans), maximumOf(greenMeans), maximumOf(blueMeans), maximumOf(lumaMeans)});
}

ChangeGraphSeries buildChangeGraphSeries(const AnalysisImage &analysisImage,
                                         ChangeGraphDirection direction) {
  ChangeGraphSeries series;
  series.label = analysisImage.label;
  series.grayscale = analysisImage.sampler.isGrayscale(analysisImage.image);
  series.selectionApplied = analysisImage.selectionRect && !analysisImage.selectionRect->isEmpty();

  const QRect sourceRect = analysisRect(analysisImage);
  if (sourceRect.isEmpty()) {
    return series;
  }

  const int width = sourceRect.width();
  const int height = sourceRect.height();
  series.origin =
      direction == ChangeGraphDirection::Horizontal ? sourceRect.left() : sourceRect.top();
  series.samplesPerPoint = direction == ChangeGraphDirection::Horizontal ? height : width;

  // 集計はチャネルごとにまとめて平面を作ってから畳む。画素ごとに取り出すと、
  // 型と色モデルの判定が集計そのものより重くなる。3 チャネルを同時に持つと
  // 4K で 100 MiB を超えるため、1 つずつ作って畳む。
  QVector<float> *means[3] = {&series.redMeans, &series.greenMeans, &series.blueMeans};
  for (int channel = 0; channel < 3; ++channel) {
    *means[channel] = foldToMeans(analysisImage.sampler.buildChannelPlane(sourceRect, channel),
                                  width, height, direction);
  }

  // 輝度は 0-255 スケールで返るため、他のチャネルと同じ 0.0-1.0 へそろえる。
  QVector<float> lumaPlane = analysisImage.sampler.buildLumaPlane(sourceRect);
  for (float &value : lumaPlane) {
    value /= 255.0F;
  }
  series.lumaMeans = foldToMeans(lumaPlane, width, height, direction);

  return series;
}

}  // namespace heiran::ui
