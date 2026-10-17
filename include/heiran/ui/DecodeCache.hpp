#pragma once

#include "heiran/infra/ImageDecoder.hpp"

#include <QHash>
#include <QSet>
#include <QString>
#include <QStringList>

namespace heiran::ui {

class DecodeCache {
public:
  static constexpr qint64 kDefaultBudgetBytes = 1024LL * 1024LL * 1024LL;

  explicit DecodeCache(qint64 budgetBytes = defaultBudgetBytes());

  static qint64 defaultBudgetBytes();
  // `subImageIndex` はページ (TIFF など) を指す。フレーム (GIF など) は全フレームを
  // 1 つのエントリに持つため、番号を付けない。0 のときは鍵に足さない。
  static QString makeKey(const QString &filePath, bool honorExifOrientation, int subImageIndex = 0);
  static qint64 entryBytes(const heiran::infra::DecodeResult &result);

  void setBudgetBytes(qint64 budgetBytes);
  qint64 budgetBytes() const;
  qint64 usedBytes() const;
  int entryCount() const;

  // 引けた回数と、引けずに復号へ回した回数。効いているかを測るために出す。
  int hitCount() const { return hitCount_; }
  int missCount() const { return missCount_; }
  void resetCounters();
  // 非同期に読みに行った場合など、`value()` を通らない経路から数える。
  void recordMiss() { ++missCount_; }

  void setRetainedKeys(const QSet<QString> &retainedKeys);

  // 解析用バッファを持つエントリのうち、当面使う見込みのある範囲を伝える。
  //
  // 範囲の外でも捨てはしない。捨てるかどうかは予算で決め、範囲は「予算を超えた
  // ときに最初に捨てる候補」を表すだけにする。1 枚が大きいため、他より先に
  // 捨てたい。
  //
  // 解析用バッファを落として表示用画像だけ残しても、セルの表示には
  // 画素値表示と RGBA16F テクスチャの両方で解析用バッファが要るため使えない。
  void setAnalysisWindowKeys(const QSet<QString> &analysisWindowKeys);

  // 先読みの範囲を決めるために使う。LRU の順序は動かさない。
  bool hasAnalysisBuffer(const QString &cacheKey) const;

  void clear();
  // 失敗した結果を捨てて再試行できるようにする。
  void remove(const QString &cacheKey);
  bool contains(const QString &filePath, bool honorExifOrientation, int subImageIndex = 0) const;
  bool containsKey(const QString &cacheKey) const;
  heiran::infra::DecodeResult value(const QString &filePath, bool honorExifOrientation,
                                    int subImageIndex = 0);
  // 引けた回数にも LRU の順にも影響させずに引く。無ければ nullptr。
  // 表示の組み立てを何度やり直しても、効いているかの計測を歪めない。
  const heiran::infra::DecodeResult *peek(const QString &cacheKey) const;
  void insert(const QString &cacheKey, const heiran::infra::DecodeResult &result);

private:
  struct Entry {
    heiran::infra::DecodeResult result;
    qint64 bytes = 0;
  };

  void touch(const QString &cacheKey);
  void removeEntry(const QString &cacheKey);
  void evictUntilWithinBudget(const QString &protectedKey);
  void evictPass(const QString &protectedKey, bool includeRetained, bool onlyOutsideAnalysisWindow);

  QHash<QString, Entry> entries_;
  QStringList order_;
  QSet<QString> retainedKeys_;
  QSet<QString> analysisWindowKeys_;
  int hitCount_ = 0;
  int missCount_ = 0;
  // 範囲が一度も設定されていない間は insert を制限しない。
  bool analysisWindowSet_ = false;
  qint64 budgetBytes_ = kDefaultBudgetBytes;
  qint64 usedBytes_ = 0;
};

}  // namespace heiran::ui
