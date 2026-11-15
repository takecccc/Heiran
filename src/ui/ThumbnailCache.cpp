#include "heiran/ui/ThumbnailCache.hpp"

#include "heiran/infra/UserDataPaths.hpp"
#include "heiran/ui/ThumbnailLoader.hpp"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QSaveFile>
#include <algorithm>
#include <vector>

namespace heiran::ui {

namespace {

// 形式や作り方を変えたら上げる。古い項目は鍵が変わり、使われずに追い出される。
constexpr int kCacheVersion = 1;

// 何枚書くごとに整理するか。
constexpr int kPruneInterval = 64;

// 整理は 1 度に 1 スレッドだけが行う。同時に一覧を数えて消すと、消した数が食い違う。
QMutex &pruneMutex() {
  static QMutex mutex;
  return mutex;
}

}  // namespace

ThumbnailCache::ThumbnailCache(QString directory, qint64 maxBytes)
    : directory_(std::move(directory)), maxBytes_(std::max(qint64{0}, maxBytes)) {}

ThumbnailCache &ThumbnailCache::standard() {
  static ThumbnailCache cache(
      QDir(heiran::infra::userDataDirectory()).filePath("cache/thumbnails"));
  return cache;
}

QString ThumbnailCache::entryPath(const QString &filePath, const QSize &maximumSize,
                                  bool honorExifOrientation) const {
  const QFileInfo fileInfo(filePath);
  if (filePath.isEmpty() || !fileInfo.exists()) {
    return {};
  }

  const QByteArray identity = QString("%1|%2|%3|%4|%5x%6|%7")
                                  .arg(kCacheVersion)
                                  .arg(fileInfo.absoluteFilePath())
                                  .arg(fileInfo.lastModified().toMSecsSinceEpoch())
                                  .arg(fileInfo.size())
                                  .arg(maximumSize.width())
                                  .arg(maximumSize.height())
                                  .arg(honorExifOrientation ? 1 : 0)
                                  .toUtf8();
  const QString hash =
      QString::fromLatin1(QCryptographicHash::hash(identity, QCryptographicHash::Sha1).toHex());
  return QDir(directory_).filePath(hash + ".png");
}

QImage ThumbnailCache::lookup(const QString &filePath, const QSize &maximumSize,
                              bool honorExifOrientation) const {
  const QString path = entryPath(filePath, maximumSize, honorExifOrientation);
  if (path.isEmpty() || !QFileInfo::exists(path)) {
    return {};
  }

  QImage thumbnail(path);
  if (thumbnail.isNull()) {
    // 書きかけや壊れた項目。作り直せるため、残さない。
    QFile::remove(path);
    return {};
  }

  // 使われた順で追い出すため、読んだ印を付ける。
  QFile file(path);
  if (file.open(QIODevice::ReadWrite)) {
    file.setFileTime(QDateTime::currentDateTime(), QFileDevice::FileModificationTime);
  }
  return thumbnail;
}

bool ThumbnailCache::contains(const QString &filePath, const QSize &maximumSize,
                              bool honorExifOrientation) const {
  const QString path = entryPath(filePath, maximumSize, honorExifOrientation);
  return !path.isEmpty() && QFileInfo::exists(path);
}

QImage ThumbnailCache::loadOrCreate(const QString &filePath, const QSize &maximumSize,
                                    bool honorExifOrientation) {
  const QString path = entryPath(filePath, maximumSize, honorExifOrientation);
  if (path.isEmpty()) {
    return {};
  }

  const QImage cached = lookup(filePath, maximumSize, honorExifOrientation);
  if (!cached.isNull()) {
    return cached;
  }

  const QImage thumbnail = loadThumbnail(filePath, maximumSize, honorExifOrientation);
  if (!thumbnail.isNull()) {
    store(path, thumbnail);
  }
  return thumbnail;
}

void ThumbnailCache::store(const QString &entryPath, const QImage &thumbnail) {
  if (!QDir().mkpath(directory_)) {
    return;
  }

  // 一時ファイルへ書いてから置き換える。読む側が書きかけを見ない。
  QSaveFile file(entryPath);
  if (!file.open(QIODevice::WriteOnly)) {
    return;
  }
  if (!thumbnail.save(&file, "PNG")) {
    file.cancelWriting();
    return;
  }
  file.commit();

  if (++storesSincePrune_ >= kPruneInterval) {
    storesSincePrune_ = 0;
    prune();
  }
}

void ThumbnailCache::prune() {
  const QMutexLocker locker(&pruneMutex());

  struct Entry {
    QString path;
    qint64 bytes = 0;
    QDateTime lastUsed;
  };
  std::vector<Entry> entries;
  qint64 totalBytes = 0;
  const QFileInfoList files =
      QDir(directory_).entryInfoList({"*.png"}, QDir::Files | QDir::NoDotAndDotDot);
  entries.reserve(static_cast<size_t>(files.size()));
  for (const QFileInfo &file : files) {
    entries.push_back({file.absoluteFilePath(), file.size(), file.lastModified()});
    totalBytes += file.size();
  }
  if (totalBytes <= maxBytes_) {
    return;
  }

  std::sort(entries.begin(), entries.end(),
            [](const Entry &left, const Entry &right) { return left.lastUsed < right.lastUsed; });
  const qint64 target = maxBytes_ / 5 * 4;
  for (const Entry &entry : entries) {
    if (totalBytes <= target) {
      break;
    }
    if (QFile::remove(entry.path)) {
      totalBytes -= entry.bytes;
    }
  }
}

}  // namespace heiran::ui
