#pragma once

#include <QCoreApplication>
#include <QDir>
#include <QStandardPaths>

namespace heiran::infra {

inline QString userDataDirectory() {
  QString base = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
  if (base.isEmpty()) {
    base = QDir::tempPath();
  }
  const auto organization = QCoreApplication::organizationName();
  return QDir(base).filePath(organization.isEmpty() ? QStringLiteral("Heiran") : organization);
}

}  // namespace heiran::infra
