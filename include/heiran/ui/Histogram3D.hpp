#pragma once

#include "heiran/ui/ViewerGridWidget.hpp"

#include <QColor>
#include <QString>
#include <QVector>

namespace heiran::ui {

// RGB の 3 次元ヒストグラム。各軸を `binsPerAxis` 個に分けた立方体のビンへ、画素を数える。
// 1 次元のヒストグラム (チャネルごと) では分からない、色の組み合わせの分布を見る。
struct Histogram3D {
  QString label;
  int binsPerAxis = 0;
  // 各軸の上限。整数画像は 1.0。浮動小数画像は実データの最大値 (1.0 を超えうる)。
  double axisMaximum = 1.0;
  // ビンごとの画素数。`histogram3DIndex()` で引く。
  QVector<int> counts;
  qint64 pixelCount = 0;
  int maxCount = 0;
  int occupiedBinCount = 0;
  bool selectionApplied = false;
};

// ビンの総数が膨れすぎないための上限。64 なら 26 万個。
inline constexpr int kMinHistogram3DBins = 2;
inline constexpr int kMaxHistogram3DBins = 64;

int clampHistogram3DBins(int binsPerAxis);

// 値 (0.0 - axisMaximum) が入るビンの番号。上限ちょうどの値は最後のビンへ入れる。
int histogram3DBinForValue(double value, double axisMaximum, int binsPerAxis);

int histogram3DIndex(int redBin, int greenBin, int blueBin, int binsPerAxis);
void histogram3DSplit(int index, int binsPerAxis, int *redBin, int *greenBin, int *blueBin);

// ビンが受け持つ値の範囲。
double histogram3DBinLow(int bin, double axisMaximum, int binsPerAxis);
double histogram3DBinHigh(int bin, double axisMaximum, int binsPerAxis);

// ビンの中心の色。上限が 1.0 を超える軸でも、表示できる範囲 (1.0) へ収める。
QColor histogram3DBinColor(int redBin, int greenBin, int blueBin, double axisMaximum,
                           int binsPerAxis);

// 解析対象 (選択範囲があればその中) から作る。
// チャネルごとに平面を 1 つずつ作ってビンの番号 (1 バイト) へ畳み、並列に数える。
// 3 つの平面を同時には持たない。
Histogram3D buildHistogram3D(const AnalysisImage &analysisImage, int binsPerAxis,
                             double axisMaximum);

// 立方体の中の点 (各成分 0.0 - 1.0) を、見る向き (水平に回す yaw、上下に振る pitch、度) で
// 画面へ写す。原点は立方体の中心。画面の x は右、y は下が正。depth は大きいほど手前。
// 長さの単位は立方体の一辺が 1。
struct Histogram3DPoint {
  double x = 0.0;
  double y = 0.0;
  double depth = 0.0;
};

Histogram3DPoint projectHistogram3DPoint(double red, double green, double blue, double yawDegrees,
                                         double pitchDegrees);

}  // namespace heiran::ui
