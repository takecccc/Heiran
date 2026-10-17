#pragma once

#include <QImage>
#include <QSize>
#include <QString>
#include <QtTypes>
#include <atomic>

namespace heiran::ui {

// 一覧のサムネイルをディスクへ取っておく。
//
// 一覧は窓を開くたびに全ページを読み直していた。4K の PNG は 1 枚 20 ms 前後かかり、
// 数百枚の連番では開くたびに数秒待つ。縮めた結果は小さい (160x120 で数 KB) ため、
// ディスクへ取っておけば 2 回目からは読み直さずに済む。
//
// 鍵は元ファイルの場所、更新時刻、大きさ、要求した大きさ、EXIF の扱い。
// **ファイルが変われば鍵も変わる** ため、古い項目を無効にする処理は要らない。
// 使われなくなった項目は容量の上限で追い出される。
//
// 複数のスレッドから使える。書き込みは一時ファイルを経由するため、読む側が
// 書きかけを見ることは無い。
class ThumbnailCache {
public:
  static constexpr qint64 kDefaultMaxBytes = 256LL * 1024LL * 1024LL;

  explicit ThumbnailCache(QString directory, qint64 maxBytes = kDefaultMaxBytes);

  // アプリのキャッシュ領域を使う共有の取っておき先。
  static ThumbnailCache &standard();

  // 取っておいたものを返す。無ければ作って取っておく。
  // 元が読めない場合は空を返し、何も取っておかない。
  QImage loadOrCreate(const QString &filePath, const QSize &maximumSize,
                      bool honorExifOrientation = true);

  // 取っておいたものだけを返す。作らない。
  QImage lookup(const QString &filePath, const QSize &maximumSize,
                bool honorExifOrientation = true) const;

  bool contains(const QString &filePath, const QSize &maximumSize,
                bool honorExifOrientation = true) const;

  // 上限を超えていれば、最後に使われた順が古いものから消す。
  // 上限の 8 割まで減らす。上限ちょうどで止めると、次の 1 枚でまた消すことになる。
  void prune();

  // 項目のファイル。元ファイルが無い場合は空。テストと診断のために公開している。
  QString entryPath(const QString &filePath, const QSize &maximumSize,
                    bool honorExifOrientation = true) const;

  const QString &directory() const { return directory_; }
  qint64 maxBytes() const { return maxBytes_; }

private:
  void store(const QString &entryPath, const QImage &thumbnail);

  QString directory_;
  qint64 maxBytes_;
  // 書くたびに整理すると一覧を数えるだけで遅くなる。一定の回数ごとにまとめて行う。
  std::atomic<int> storesSincePrune_{0};
};

}  // namespace heiran::ui
