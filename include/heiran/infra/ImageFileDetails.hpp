#pragma once

#include <QSize>
#include <QString>

namespace heiran::infra {

struct ImageFileDetails {
  QSize dimensions;
  qint64 fileSize = -1;
  QString error;
};

ImageFileDetails readImageFileDetails(const QString &path);

}  // namespace heiran::infra
