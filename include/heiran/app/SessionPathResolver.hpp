#pragma once

#include "heiran/domain/SessionModel.hpp"

#include <QVector>

namespace heiran::app {

class SessionPathResolver {
public:
  static QVector<QString> resolveGlobbedPaths(const QString &sessionDirectory,
                                              const QString &globPath);
  static void resolveImagePaths(heiran::domain::SessionModel *session);
};

}  // namespace heiran::app
