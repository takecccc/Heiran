#pragma once

#include <QStringList>
#include <atomic>

namespace heiran::infra {

struct FavoriteCopyResult {
  QStringList copiedPaths;
  QStringList errors;
  int skipped = 0;
  bool cancelled = false;
};

FavoriteCopyResult copyFavoriteFiles(const QStringList &paths, const QString &directory,
                                     const std::atomic_bool &cancel);

}  // namespace heiran::infra
