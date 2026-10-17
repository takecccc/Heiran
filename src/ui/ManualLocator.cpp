#include "heiran/ui/ManualLocator.hpp"

#include <QDir>
#include <QFileInfo>
#include <QStringList>

namespace heiran::ui {

QString findManualIndex(const QString &applicationDir) {
  const QDir directory(applicationDir);
  const QStringList candidates = {
      QStringLiteral("../doc/manual/index.html"),
      QStringLiteral("doc/manual/index.html"),
      QStringLiteral("manual/index.html"),
  };
  for (const QString &candidate : candidates) {
    const QFileInfo info(directory.filePath(candidate));
    if (info.isFile()) {
      return info.canonicalFilePath();
    }
  }
  return {};
}

}  // namespace heiran::ui
