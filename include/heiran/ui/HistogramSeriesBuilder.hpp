#pragma once

#include "heiran/ui/ViewerGridWidget.hpp"

#include <QString>
#include <QVector>

namespace heiran::ui {

// 1 チャネル分の統計値。ビンからではなく画素の値から求める。
// ビンは 256 段に丸めた後の分布のため、平均や標準偏差をそこから出すと誤差が乗る。
struct HistogramChannelStatistics {
  double mean = 0.0;
  double standardDeviation = 0.0;
  double minimum = 0.0;
  double maximum = 0.0;
  // 最も画素数の多いビンの値と、その割合。
  double peakValue = 0.0;
  double peakRatio = 0.0;
};

struct HistogramSeries {
  QString label;
  QVector<int> redBins;
  QVector<int> greenBins;
  QVector<int> blueBins;
  bool grayscale = false;
  bool selectionApplied = false;
  // 集計した画素数。縦軸をこれで割って割合として描く。
  // 画像の大きさが違うセルを並べても、同じ尺度で比べられる。
  qint64 pixelCount = 0;
  HistogramChannelStatistics redStatistics;
  HistogramChannelStatistics greenStatistics;
  HistogramChannelStatistics blueStatistics;
};

// 浮動小数画像は 1.0 を超える値を持ちうるため、実データの最大値を調べる。
// 整数画像は定義上 1.0 が上限なので走査しない。
double histogramSampleMaximum(const AnalysisImage &analysisImage);

// 解析対象からヒストグラムの系列と統計値を作る。
// `axisMaximum` は全セル共通の横軸の上限。セルごとに軸が変わると比べられない。
HistogramSeries buildHistogramSeries(const AnalysisImage &analysisImage, double axisMaximum);

}  // namespace heiran::ui
