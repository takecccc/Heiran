#pragma once

#include "heiran/ui/ViewerGridWidget.hpp"

#include <QList>
#include <QRect>
#include <algorithm>

namespace heiran::ui {

// 解析入力の同一性。取っておいた結果を使い回してよいかの判断に使う。
//
// ファイルの経路では、同じファイルを別のセルで開いた場合や再読込で中身が変わった
// 場合に取り違える。`QImage::cacheKey()` は画像の実体ごとに異なる値になるため、
// これを使う。解析用バッファの番地だけでは、解放された領域が再利用されたときに
// 別の画像へ同じ番地が付く。3 つが一致することを求めれば、その取り違えは起きない。
//
// 入れるのは解析に影響するものだけにする。表示側の露出やチャネル表示は入れない。
// 解析は解析用バッファを直接読むため、これらでは結果が変わらない。回転と反転は
// 走査する並びが変わるため入れる。
struct AnalysisSourceKey {
  qint64 imageCacheKey = 0;
  quintptr analysisBufferAddress = 0;
  qsizetype analysisBufferBytes = 0;
  int rotationQuarterTurns = 0;
  bool flipX = false;
  bool flipY = false;
  // 空なら画像全体。
  QRect selectionRect;

  bool operator==(const AnalysisSourceKey &other) const = default;
};

AnalysisSourceKey makeAnalysisSourceKey(const AnalysisImage &analysisImage);

// 解析結果を取っておく。解析は全画素を走査するため、ページを行き来したり
// 選択範囲を出したり消したりするたびに数え直すのは高くつく。
//
// `Variant` は同じ入力でも結果が変わる条件。ヒストグラムでは横軸の上限、
// 変化グラフでは平均を取る向きが入る。
template <typename Value, typename Variant> class AnalysisResultCache {
public:
  // 既定の保持数。解析結果はいずれも数 KiB のため、件数だけで抑えれば足りる。
  static constexpr int kDefaultCapacity = 64;

  explicit AnalysisResultCache(int capacity = kDefaultCapacity)
      : capacity_(std::max(1, capacity)) {}

  // 取っておいた結果があれば返す。無ければ `compute` を呼んで入れる。
  template <typename Compute>
  Value value(const AnalysisImage &analysisImage, const Variant &variant, Compute compute) {
    const AnalysisSourceKey key = makeAnalysisSourceKey(analysisImage);

    for (int index = 0; index < entries_.size(); ++index) {
      const auto &candidate = entries_.at(index);
      if (candidate.key != key || !(candidate.variant == variant)) {
        continue;
      }
      // 先頭が直近に使ったもの。
      const Entry entry = entries_.takeAt(index);
      entries_.prepend(entry);
      return entry.value;
    }

    ++computeCount_;
    Value value = compute();
    entries_.prepend({.key = key, .variant = variant, .value = value});
    while (entries_.size() > capacity_) {
      entries_.removeLast();
    }
    return value;
  }

  void clear() { entries_.clear(); }

  int entryCount() const { return static_cast<int>(entries_.size()); }

  // 実際に計算した回数。取っておけたかどうかをテストと計測で見るために出す。
  int computeCount() const { return computeCount_; }

private:
  struct Entry {
    AnalysisSourceKey key;
    Variant variant;
    Value value;
  };

  int capacity_;
  int computeCount_ = 0;
  // 件数が少ないため線形に探す。64 件の比較は、避けたい走査 (数千万画素) に
  // 比べれば無視できる。
  QList<Entry> entries_;
};

}  // namespace heiran::ui
