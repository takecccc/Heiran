#pragma once

#include "heiran/domain/SessionModel.hpp"

#include <QString>

namespace heiran::infra {

struct HrnSaveResult {
  bool ok = false;
  QString errorMessage;
};

class HrnConfigWriter {
public:
  HrnSaveResult saveToFile(const heiran::domain::SessionDocument &document,
                           const QString &filePath) const;
};

}  // namespace heiran::infra
