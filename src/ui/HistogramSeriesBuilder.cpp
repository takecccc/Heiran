#include "heiran/ui/HistogramSeriesBuilder.hpp"

#include "heiran/infra/ParallelRows.hpp"

#include <QMutex>
#include <QMutexLocker>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace heiran::ui {

QRect histogramAnalysisRect(const AnalysisImage &analysisImage) {
  const QRect imageRect = analysisImage.image.rect();
  return analysisImage.selectionRect.value_or(imageRect).intersected(imageRect);
}

// 浮動小数画像は 1.0 を超える値を持ちうるため、実データの最大値を調べる。
// 整数画像は定義上 1.0 が上限なので走査しない。
double histogramSampleMaximum(const AnalysisImage &analysisImage) {
  const auto sampleType = analysisImage.sampler.decodedImage.hasAnalysisBuffer()
                              ? analysisImage.sampler.decodedImage.analysisBuffer.sampleType
                              : heiran::infra::PixelSampleType::UInt8;
  if (sampleType != heiran::infra::PixelSampleType::Float16 &&
      sampleType != heiran::infra::PixelSampleType::Float32) {
    return 1.0;
  }

  // 画素ごとに取り出すと、型と色モデルの判定を毎回通る。チャネルごとにまとめて
  // 作ってから畳む。3 つ同時に持つと 4K で 100 MiB を超えるため 1 つずつ扱う。
  const QRect sourceRect = histogramAnalysisRect(analysisImage);
  double maximum = 1.0;
  QMutex mutex;
  for (int channel = 0; channel < 3; ++channel) {
    const QVector<float> plane = analysisImage.sampler.buildChannelPlane(sourceRect, channel);
    // 最大は、帯ごとに求めて、最後にまとめる。
    const int regionWidth = std::max(1, sourceRect.width());
    const int regionHeight = static_cast<int>(plane.size() / regionWidth);
    heiran::infra::forEachRowBand(regionHeight, [&](int firstRow, int lastRow) {
      double local = 1.0;
      for (qsizetype index = static_cast<qsizetype>(firstRow) * regionWidth;
           index < static_cast<qsizetype>(lastRow) * regionWidth && index < plane.size(); ++index) {
        local = std::max(local, static_cast<double>(plane.at(index)));
      }
      const QMutexLocker locker(&mutex);
      maximum = std::max(maximum, local);
    });
  }
  return maximum;
}

HistogramSeries buildHistogramSeries(const AnalysisImage &analysisImage, double axisMaximum) {
  HistogramSeries series;
  series.label = analysisImage.label;
  series.redBins = QVector<int>(256, 0);
  series.greenBins = QVector<int>(256, 0);
  series.blueBins = QVector<int>(256, 0);
  series.grayscale = analysisImage.sampler.isGrayscale(analysisImage.image);
  series.selectionApplied = analysisImage.selectionRect.has_value();

  const QRect sourceRect = histogramAnalysisRect(analysisImage);

  // 集計は表示用画像ではなく解析用バッファから行う。
  // 横軸は全セル共通の 0.0 - axisMaximum とし、浮動小数画像の 1.0 超も収める。
  const float axisScale = static_cast<float>(axisMaximum > 0.0 ? 255.0 / axisMaximum : 255.0);
  // std::lround は関数呼び出しになり、全画素で呼ぶと集計の大半を占める。
  // 0.5 を足して切り捨てれば同じ結果になる。負の値は切り捨ての向きが違うが、
  // どちらも下限で丸め込まれるため結果は変わらない。
  const auto binIndex = [axisScale](float value) {
    return std::clamp(static_cast<int>(value * axisScale + 0.5F), 0, 255);
  };

  // 集計もチャネルごとにまとめて作ってから畳む。画素ごとに取り出すと、型と
  // 色モデルの判定が集計そのものより重くなる。
  QVector<int> *bins[3] = {&series.redBins, &series.greenBins, &series.blueBins};
  HistogramChannelStatistics *statistics[3] = {&series.redStatistics, &series.greenStatistics,
                                               &series.blueStatistics};
  for (int channel = 0; channel < 3; ++channel) {
    const QVector<float> plane = analysisImage.sampler.buildChannelPlane(sourceRect, channel);
    if (plane.isEmpty()) {
      continue;
    }

    QVector<int> &channelBins = *bins[channel];
    // 平均と標準偏差はビンからではなく画素の値から求める。ビンは 256 段へ
    // 丸めた後の分布のため、そこから出すと丸めの分だけ誤差が乗る。
    //
    // 集計は、行の帯ごとに自分の表 (ビン、合計、二乗和、最小、最大) へ。最後にまとめる。
    // 逐次に 800 万画素を数えると、4K で 1 チャネルあたり 30 ms 近くかかっていた。
    const int regionWidth = std::max(1, sourceRect.width());
    const int regionHeight = static_cast<int>(plane.size() / regionWidth);
    struct Partial {
      std::array<int, 256> bins{};
      double sum = 0.0;
      double sumOfSquares = 0.0;
      double minimum = std::numeric_limits<double>::infinity();
      double maximum = -std::numeric_limits<double>::infinity();
    };
    double sum = 0.0;
    double sumOfSquares = 0.0;
    double minimum = plane.at(0);
    double maximum = plane.at(0);
    QMutex mutex;
    heiran::infra::forEachRowBand(regionHeight, [&](int firstRow, int lastRow) {
      Partial partial;
      for (qsizetype index = static_cast<qsizetype>(firstRow) * regionWidth;
           index < static_cast<qsizetype>(lastRow) * regionWidth && index < plane.size(); ++index) {
        const float value = plane.at(index);
        ++partial.bins[static_cast<size_t>(binIndex(value))];
        const double sample = value;
        partial.sum += sample;
        partial.sumOfSquares += sample * sample;
        partial.minimum = std::min(partial.minimum, sample);
        partial.maximum = std::max(partial.maximum, sample);
      }
      const QMutexLocker locker(&mutex);
      for (size_t index = 0; index < partial.bins.size(); ++index) {
        channelBins[static_cast<qsizetype>(index)] += partial.bins[index];
      }
      sum += partial.sum;
      sumOfSquares += partial.sumOfSquares;
      minimum = std::min(minimum, partial.minimum);
      maximum = std::max(maximum, partial.maximum);
    });

    const double count = static_cast<double>(plane.size());
    const double mean = sum / count;
    // 分散が丸めで負に落ちることがあるため下限を 0 にする。
    const double variance = std::max(0.0, sumOfSquares / count - mean * mean);

    int peakBin = 0;
    for (int index = 1; index < channelBins.size(); ++index) {
      if (channelBins.at(index) > channelBins.at(peakBin)) {
        peakBin = index;
      }
    }

    *statistics[channel] = {
        .mean = mean,
        .standardDeviation = std::sqrt(variance),
        .minimum = minimum,
        .maximum = maximum,
        .peakValue = axisScale > 0.0F ? static_cast<double>(peakBin) / axisScale : 0.0,
        .peakRatio = static_cast<double>(channelBins.at(peakBin)) / count,
    };
    series.pixelCount = plane.size();
  }

  return series;
}

}  // namespace heiran::ui
