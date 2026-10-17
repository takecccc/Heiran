#include "heiran/ui/RecentImages.hpp"

#include <QDir>
#include <QSettings>
#include <algorithm>

namespace heiran::ui {

namespace {

constexpr auto kRecentImagesKey = "recent_images";

// 同じファイルかを見るための形。
QString comparisonKey(const QString &path) {
  const QString cleaned = QDir::cleanPath(path);
#if defined(Q_OS_WIN)
  return cleaned.toLower();
#else
  return cleaned;
#endif
}

}  // namespace

QStringList RecentImages::pushed(const QStringList &recent, const QString &path, int maxEntries) {
  if (path.isEmpty() || maxEntries <= 0) {
    return recent.mid(0, std::max(0, maxEntries));
  }

  QStringList result = removed(recent, path);
  result.prepend(path);
  return result.mid(0, maxEntries);
}

QStringList RecentImages::removed(const QStringList &recent, const QString &path) {
  const QString key = comparisonKey(path);
  QStringList result;
  for (const QString &entry : recent) {
    if (comparisonKey(entry) != key) {
      result.push_back(entry);
    }
  }
  return result;
}

QStringList RecentImages::load() {
  return QSettings().value(kRecentImagesKey).toStringList().mid(0, kMaxEntries);
}

void RecentImages::save(const QStringList &recent) {
  QSettings().setValue(kRecentImagesKey, recent.mid(0, kMaxEntries));
}

}  // namespace heiran::ui
