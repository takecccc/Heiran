#include "heiran/infra/FavoriteStore.hpp"
#include "heiran/infra/UserDataPaths.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QSaveFile>
#include <algorithm>
#include <utility>

namespace heiran::infra {

FavoriteStore::FavoriteStore(QString filePath) : filePath_(std::move(filePath)) {
  if (filePath_.isEmpty()) {
    filePath_ = QDir(userDataDirectory()).filePath("favorites.json");
  }
}

QString FavoriteStore::pathKey(const QString &path) {
  if (path.isEmpty()) {
    return {};
  }
  QString key = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
#ifdef Q_OS_WIN
  key = key.toCaseFolded();
#endif
  return key;
}

bool FavoriteStore::contains(const QString &path) const {
  return !path.isEmpty() && paths_.contains(pathKey(path));
}

bool FavoriteStore::load(QString *error) {
  QFile file(filePath_);
  if (!file.exists()) {
    paths_.clear();
    return true;
  }
  if (!file.open(QIODevice::ReadOnly)) {
    if (error) {
      *error = file.errorString();
    }
    return false;
  }
  QJsonParseError parseError;
  const auto document = QJsonDocument::fromJson(file.readAll(), &parseError);
  const auto object = document.object();
  bool valid = parseError.error == QJsonParseError::NoError && document.isObject() &&
               object.value("version").toInt() == 1 && object.value("files").isArray();
  QSet<QString> paths;
  for (const auto &value : object.value("files").toArray()) {
    if (!value.isString() || !QFileInfo(value.toString()).isAbsolute()) {
      valid = false;
      break;
    }
    paths.insert(pathKey(value.toString()));
  }
  if (!valid) {
    if (error) {
      *error = QCoreApplication::translate("FavoriteStore", "Invalid favorites data.");
    }
    return false;
  }
  paths_ = std::move(paths);
  return true;
}

bool FavoriteStore::setFavorite(const QString &path, bool favorite, QString *error) {
  return setFavorites({path}, favorite, error);
}

bool FavoriteStore::setFavorites(const QStringList &paths, bool favorite, QString *error) {
  QSet<QString> keys;
  for (const auto &path : paths) {
    if (!path.isEmpty()) {
      keys.insert(pathKey(path));
    }
  }
  if (keys.isEmpty()) {
    return false;
  }
  if (!QDir().mkpath(QFileInfo(filePath_).absolutePath())) {
    if (error) {
      *error =
          QCoreApplication::translate("FavoriteStore", "Could not create the favorites directory.");
    }
    return false;
  }
  QLockFile lock(filePath_ + ".lock");
  if (!lock.tryLock(100)) {
    if (error) {
      *error = QCoreApplication::translate(
          "FavoriteStore", "Favorites are being updated by another window. Try again.");
    }
    return false;
  }
  FavoriteStore latest(filePath_);
  if (!latest.load(error)) {
    return false;
  }
  for (const auto &key : keys) {
    if (favorite) {
      latest.paths_.insert(key);
    } else {
      latest.paths_.remove(key);
    }
  }
  auto sorted = latest.paths_.values();
  std::sort(sorted.begin(), sorted.end());
  QJsonArray files;
  for (const auto &entry : sorted) {
    files.append(entry);
  }
  QSaveFile output(filePath_);
  const QByteArray bytes = QJsonDocument(QJsonObject{{"version", 1}, {"files", files}}).toJson();
  if (!output.open(QIODevice::WriteOnly) || output.write(bytes) != bytes.size() ||
      !output.commit()) {
    if (error) {
      *error = output.errorString();
    }
    return false;
  }
  paths_ = std::move(latest.paths_);
  return true;
}

}  // namespace heiran::infra
