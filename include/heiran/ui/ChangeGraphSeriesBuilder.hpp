#pragma once

#include "heiran/ui/ViewerGridWidget.hpp"

#include <QString>
#include <QVector>

namespace heiran::ui {

// 平均を取る向き。
enum class ChangeGraphDirection {
  // 縦に平均して、横方向の変化を見る。横軸は列の位置。
  Horizontal,
  // 横に平均して、縦方向の変化を見る。横軸は行の位置。
  Vertical,
};

// 1 セル分の変化グラフ。位置ごとの平均を並べる。
struct ChangeGraphSeries {
  QString label;
  // 値は 0.0-1.0 へ正規化する。浮動小数画像では 1.0 を超えうる。
  QVector<float> redMeans;
  QVector<float> greenMeans;
  QVector<float> blueMeans;
  QVector<float> lumaMeans;
  bool grayscale = false;
  bool selectionApplied = false;
  // 横軸の左端にあたる画像座標。選択範囲を指定した場合はその左端または上端。
  // 目盛りを画像座標で出すために持つ。
  int origin = 0;
  // 平均を取った画素数。1 点あたりの標本数で、選択範囲の高さまたは幅にあたる。
  int samplesPerPoint = 0;

  // 全チャネルを通した最大値。縦軸の上限を決めるために使う。
  float maximumMean() const;
};

// 解析対象から変化グラフを作る。
ChangeGraphSeries buildChangeGraphSeries(const AnalysisImage &analysisImage,
                                         ChangeGraphDirection direction);

}  // namespace heiran::ui
