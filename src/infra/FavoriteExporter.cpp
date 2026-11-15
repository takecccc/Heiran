#include "heiran/infra/FavoriteExporter.hpp"
#include "heiran/infra/FavoriteStore.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QTemporaryFile>

namespace heiran::infra {

FavoriteCopyResult copyFavoriteFiles(const QStringList &paths, const QString &directory,
                                     const std::atomic_bool &cancel) {
  FavoriteCopyResult result;
  QSet<QString> seen;
  const QDir destination(directory);
  for (const auto &path : paths) {
    if (cancel.load()) {
      result.cancelled = true;
      break;
    }
    const auto key = FavoriteStore::pathKey(path);
    if (seen.contains(key)) {
      continue;
    }
    seen.insert(key);
    const QFileInfo info(path);
    const QString initialTarget = destination.filePath(info.fileName());
    if (FavoriteStore::pathKey(initialTarget) == key) {
      ++result.skipped;
      continue;
    }
    QFile input(path);
    QTemporaryFile output(destination.filePath(".heiran-copy-XXXXXX"));
    if (!input.open(QIODevice::ReadOnly) || !output.open()) {
      result.errors.push_back(path + "\n" +
                              (input.isOpen() ? output.errorString() : input.errorString()));
      continue;
    }
    bool failed = false;
    while (!input.atEnd()) {
      if (cancel.load()) {
        result.cancelled = true;
        break;
      }
      const QByteArray chunk = input.read(1024 * 1024);
      if (input.error() != QFileDevice::NoError || output.write(chunk) != chunk.size()) {
        result.errors.push_back(
            path + "\n" +
            (input.error() != QFileDevice::NoError ? input.errorString() : output.errorString()));
        failed = true;
        break;
      }
    }
    if (result.cancelled || cancel.load()) {
      result.cancelled = true;
      break;
    }
    if (failed) {
      continue;
    }
    if (!output.flush()) {
      result.errors.push_back(path + "\n" + output.errorString());
      continue;
    }
    output.close();
    QString target = initialTarget;
    int suffix = 2;
    while (true) {
      if (!QFileInfo::exists(target) && output.rename(target)) {
        output.setAutoRemove(false);
        result.copiedPaths.push_back(target);
        break;
      }
      if (!QFileInfo::exists(target)) {
        result.errors.push_back(path + "\n" + output.errorString());
        break;
      }
      const QString extension = info.suffix().isEmpty() ? QString() : "." + info.suffix();
      target = destination.filePath(
          QString("%1 (%2)%3").arg(info.completeBaseName()).arg(suffix++).arg(extension));
    }
  }
  return result;
}

}  // namespace heiran::infra
