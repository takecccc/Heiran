#pragma once

#include "heiran/ui/AnalysisResultCache.hpp"
#include "heiran/ui/HistogramSeriesBuilder.hpp"

#include <variant>

namespace heiran::ui {

// ヒストグラムの集計結果を取っておく。
//
// 集計は全画素を走査するため、4K の 1 枚で 100 ms 前後かかる。ページを行き来する、
// 選択範囲を出したり消したりする、といった操作では同じ画像を何度も数え直すことになる。
//
// ラベルは値の一部だが集計には影響しないため、鍵から外して取り出すときに入れ直す。
// ラベルを変えただけで数え直すのは無駄になる。
class HistogramSeriesCache {
public:
  // 既定の保持数。1 件あたり 3 x 256 の度数と統計値で 3 KiB ほどのため、
  // 64 件でも 200 KiB に収まる。件数だけで抑えれば足りる。
  static constexpr int kDefaultCapacity = AnalysisResultCache<int, int>::kDefaultCapacity;

  explicit HistogramSeriesCache(int capacity = kDefaultCapacity);

  // 横軸の上限。浮動小数画像では全画素の走査になるため、これも取っておく。
  double sampleMaximum(const AnalysisImage &analysisImage);

  HistogramSeries series(const AnalysisImage &analysisImage, double axisMaximum);

  void clear();

  int entryCount() const;

  // 実際に計算した回数。取っておけたかどうかをテストと計測で見るために出す。
  int computeCount() const;

private:
  // 横軸の上限には条件が無いため、変種は使わない。
  AnalysisResultCache<double, std::monostate> maximums_;
  // 軸の上限が変われば分け方が変わるため、別のものとして扱う。
  AnalysisResultCache<HistogramSeries, double> series_;
};

}  // namespace heiran::ui
