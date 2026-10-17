#include "heiran/ui/DecodeCache.hpp"

#include <QDebug>
#include <algorithm>

namespace heiran::ui {

namespace {

bool decodeCacheTraceEnabled() {
  static const bool enabled = qEnvironmentVariableIntValue("HEIRAN_TRACE_DECODER") != 0;
  return enabled;
}

}  // namespace

DecodeCache::DecodeCache(qint64 budgetBytes) : budgetBytes_(std::max<qint64>(0, budgetBytes)) {}

qint64 DecodeCache::defaultBudgetBytes() {
  static const qint64 budgetBytes = []() {
    const int overrideMib = qEnvironmentVariableIntValue("HEIRAN_DECODE_CACHE_BUDGET_MB");
    if (overrideMib > 0) {
      return static_cast<qint64>(overrideMib) * 1024LL * 1024LL;
    }
    return kDefaultBudgetBytes;
  }();

  return budgetBytes;
}

QString DecodeCache::makeKey(const QString &filePath, bool honorExifOrientation,
                             int subImageIndex) {
  QString key =
      filePath + (honorExifOrientation ? QStringLiteral("|exif=1") : QStringLiteral("|exif=0"));
  if (subImageIndex > 0) {
    key += QStringLiteral("|sub=%1").arg(subImageIndex);
  }
  return key;
}

namespace {

// 1 枚分の大きさ。表示用画像と、共有していない解析用バッファ。
qint64 singleImageBytes(const heiran::infra::DecodedImage &decodedImage) {
  qint64 bytes = decodedImage.displayImage.sizeInBytes();

  // 8bit の RGBA は、表示用 QImage と解析用バッファが同じ記憶域を共有する。
  const bool sharesStorage =
      decodedImage.analysisBuffer.isValid() && !decodedImage.displayImage.isNull() &&
      decodedImage.displayImage.constBits() == decodedImage.analysisBuffer.data();
  if (!sharesStorage) {
    bytes += decodedImage.analysisBuffer.byteCount();
  }

  return std::max<qint64>(0, bytes);
}

}  // namespace

qint64 DecodeCache::entryBytes(const heiran::infra::DecodeResult &result) {
  const auto &decodedImage = result.decodedImage;
  qint64 bytes = singleImageBytes(decodedImage);

  // 後から作るアニメーションは、全部を展開しない。抱え得る上限で数える。
  if (decodedImage.frameStream != nullptr) {
    bytes += decodedImage.frameStream->residentBytes();
  }

  // アニメーションは全フレームを持つ。先頭のフレームは結果そのものと同じ内容で、
  // 上で数えているため、2 枚目以降を足す。
  if (decodedImage.frameSequence != nullptr) {
    const auto &frames = decodedImage.frameSequence->frames;
    for (qsizetype index = 1; index < frames.size(); ++index) {
      bytes += singleImageBytes(frames.at(index));
    }
  }
  return bytes;
}

void DecodeCache::setBudgetBytes(qint64 budgetBytes) {
  budgetBytes_ = std::max<qint64>(0, budgetBytes);
  evictUntilWithinBudget(QString());
}

qint64 DecodeCache::budgetBytes() const {
  return budgetBytes_;
}

qint64 DecodeCache::usedBytes() const {
  return usedBytes_;
}

int DecodeCache::entryCount() const {
  return static_cast<int>(entries_.size());
}

void DecodeCache::setRetainedKeys(const QSet<QString> &retainedKeys) {
  retainedKeys_ = retainedKeys;
}

void DecodeCache::setAnalysisWindowKeys(const QSet<QString> &analysisWindowKeys) {
  analysisWindowKeys_ = analysisWindowKeys;
  analysisWindowSet_ = true;

  // 範囲の外だからといって捨てはしない。捨てるかどうかは予算で決める。
  // 範囲は「予算を超えたときに最初に捨てる候補」を表すだけにする。
  //
  // 以前は範囲の外を無条件で捨てていた。解析用バッファを持つ画像では、予算が
  // 1024 MiB あっても前後 1 ページ分しか残らず、ページを戻すたびに復号し直して
  // いた。4K で 1 回あたり数 ms かかる。
  evictUntilWithinBudget(QString());
}

bool DecodeCache::hasAnalysisBuffer(const QString &cacheKey) const {
  const auto it = entries_.constFind(cacheKey);
  return it != entries_.constEnd() && it->result.decodedImage.hasAnalysisBuffer();
}

void DecodeCache::resetCounters() {
  hitCount_ = 0;
  missCount_ = 0;
}

void DecodeCache::clear() {
  entries_.clear();
  order_.clear();
  usedBytes_ = 0;
}

void DecodeCache::remove(const QString &cacheKey) {
  removeEntry(cacheKey);
}

bool DecodeCache::contains(const QString &filePath, bool honorExifOrientation,
                           int subImageIndex) const {
  return containsKey(makeKey(filePath, honorExifOrientation, subImageIndex));
}

const heiran::infra::DecodeResult *DecodeCache::peek(const QString &cacheKey) const {
  const auto it = entries_.constFind(cacheKey);
  return it == entries_.constEnd() ? nullptr : &it->result;
}

bool DecodeCache::containsKey(const QString &cacheKey) const {
  return entries_.contains(cacheKey);
}

heiran::infra::DecodeResult DecodeCache::value(const QString &filePath, bool honorExifOrientation,
                                               int subImageIndex) {
  const QString cacheKey = makeKey(filePath, honorExifOrientation, subImageIndex);
  const auto it = entries_.constFind(cacheKey);
  if (it == entries_.constEnd()) {
    ++missCount_;
    return {};
  }

  ++hitCount_;
  touch(cacheKey);
  return it->result;
}

void DecodeCache::insert(const QString &cacheKey, const heiran::infra::DecodeResult &result) {
  removeEntry(cacheKey);

  const qint64 bytes = entryBytes(result);
  entries_.insert(cacheKey, {.result = result, .bytes = bytes});
  order_.push_back(cacheKey);
  usedBytes_ += bytes;

  evictUntilWithinBudget(cacheKey);
}

void DecodeCache::touch(const QString &cacheKey) {
  if (order_.removeAll(cacheKey) > 0) {
    order_.push_back(cacheKey);
  }
}

void DecodeCache::removeEntry(const QString &cacheKey) {
  const auto it = entries_.constFind(cacheKey);
  if (it == entries_.constEnd()) {
    return;
  }

  usedBytes_ -= it->bytes;
  entries_.erase(it);
  order_.removeAll(cacheKey);
}

void DecodeCache::evictUntilWithinBudget(const QString &protectedKey) {
  // 捨てる順は 3 段。
  //
  // 1. 現在ページ以外で、解析用バッファを持ち保持範囲の外にあるもの。
  //    1 枚が大きく、解析からも表示からも当面使わない。
  // 2. 現在ページ以外。
  // 3. 現在ページ分。ここまで来るのは予算が 1 ページ分にも足りない場合。
  evictPass(protectedKey, false, true);
  evictPass(protectedKey, false, false);
  evictPass(protectedKey, true, false);
}

void DecodeCache::evictPass(const QString &protectedKey, bool includeRetained,
                            bool onlyOutsideAnalysisWindow) {
  int index = 0;
  while (usedBytes_ > budgetBytes_ && index < order_.size()) {
    const QString evictedKey = order_.at(index);
    // 直前に挿入したエントリは、単独で予算を超えていても保持する。
    if (evictedKey == protectedKey || (!includeRetained && retainedKeys_.contains(evictedKey))) {
      ++index;
      continue;
    }
    if (onlyOutsideAnalysisWindow) {
      const auto entry = entries_.constFind(evictedKey);
      const bool isCandidate = entry != entries_.constEnd() &&
                               entry->result.decodedImage.hasAnalysisBuffer() &&
                               analysisWindowSet_ && !analysisWindowKeys_.contains(evictedKey);
      if (!isCandidate) {
        ++index;
        continue;
      }
    }

    const qint64 evictedBytes = entries_.value(evictedKey).bytes;
    removeEntry(evictedKey);
    if (decodeCacheTraceEnabled()) {
      qDebug().noquote() << QStringLiteral("[HeiranDecode] cache evict key=%1 bytes=%2 used=%3")
                                .arg(evictedKey)
                                .arg(evictedBytes)
                                .arg(usedBytes_);
    }
  }
}

}  // namespace heiran::ui
