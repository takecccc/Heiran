#pragma once

#include <QVector>

namespace heiran::ui {

// ヒストグラムの横軸の表示範囲。軸全体 (0.0 - 軸の上限) に対する割合で持つ。
// 軸の上限が画像によって変わっても、範囲の意味 (どのあたりを見ているか) が保たれる。
struct HistogramRange {
  double low = 0.0;
  double high = 1.0;

  double span() const { return high - low; }
  bool isFull() const { return low <= 0.0 && high >= 1.0; }
};

// 範囲として扱える最小の幅。ビン 1 つ分より狭くしても、見えるものが増えない。
inline constexpr double kMinHistogramSpan = 1.0 / 256.0;

// 範囲を [0, 1] に収め、幅が最小幅を下回らないようにする。
HistogramRange clampHistogramRange(HistogramRange range);

// 軸上の位置 (割合) を中心に、範囲の幅を `factor` 倍にする。1 より小さいと拡大 (狭める)。
// 位置の下にあるものは、ずらさずに同じ画面位置へ残る。
HistogramRange zoomHistogramRange(HistogramRange range, double anchorFraction, double factor);

// 画面の x から、軸上の位置 (割合) を求める。`left` と `width` はグラフの描画範囲。
double histogramFractionForX(double x, double left, double width, const HistogramRange &range);

// 軸上の位置 (割合) から、画面の x を求める。上の逆。
double histogramXForFraction(double fraction, double left, double width,
                             const HistogramRange &range);

// 軸上の位置 (割合) に最も近いビンの番号。`binCount` 個のビンが 0.0 - 1.0 に並ぶ。
int histogramBinForFraction(double fraction, int binCount);

// ビンの番号が表す、軸上の位置 (割合)。
double histogramFractionForBin(int bin, int binCount);

// 表示範囲の中にあるビンのうち、最も大きい画素数。縦軸の上限 (自動) を決めるのに使う。
// 範囲を狭めると、その中の細かな山が見えるよう、縦も合わせて伸びる。
int histogramMaxCountInRange(const QVector<int> &bins, const HistogramRange &range);

// 縦軸の上限 (割合)。`fixedRatio` が正なら、それを使う (全セルで同じ縦の尺度にする)。
// そうでなければ `autoRatio` (そのセルの最大)。
double histogramFullScaleRatio(double autoRatio, double fixedRatio);

}  // namespace heiran::ui
