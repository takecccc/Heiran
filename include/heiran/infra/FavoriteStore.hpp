#pragma once

#include <QSet>
#include <QString>
#include <QStringList>

namespace heiran::infra {

class FavoriteStore {
public:
  explicit FavoriteStore(QString filePath = {});
  static QString pathKey(const QString &path);
  bool load(QString *error = nullptr);
  bool contains(const QString &path) const;
  bool setFavorite(const QString &path, bool favorite, QString *error = nullptr);
  bool setFavorites(const QStringList &paths, bool favorite, QString *error = nullptr);
  QString filePath() const { return filePath_; }

private:
  QString filePath_;
  QSet<QString> paths_;
};

}  // namespace heiran::infra
